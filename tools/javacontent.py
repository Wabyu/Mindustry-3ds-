"""Extract Mindustry content definitions straight from the upstream Java sources.

Mindustry declares its content with "double brace" initialisers, e.g.

    mechanicalDrill = new Drill("mechanical-drill"){{
        requirements(Category.production, with(Items.copper, 12));
        tier = 2;
        drillTime = 600;
        size = 2;
    }};

This module parses those blocks (Blocks.java, Items.java, UnitTypes.java ...)
and the field defaults of the classes they instantiate (Drill.java,
Block.java ...), so the 3DS port uses the exact values of the pinned upstream
revision instead of numbers copied by hand.

It is deliberately a small, strict parser: anything it cannot evaluate is
reported as None, never guessed.
"""
import ast
import os
import re

PRIMS = ("float", "int", "boolean", "double", "byte", "short", "long")


# --------------------------------------------------------------------------
# Lexical helpers

def strip_comments(src):
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            out.append(src[i:j + 1]); i = j + 1
        elif src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(c); i += 1
    return "".join(out)


def match_brace(src, i):
    """src[i] is an opening bracket; return index of its matching closer."""
    pairs = {"(": ")", "{": "}", "[": "]"}
    stack = [pairs[src[i]]]
    j = i + 1
    while j < len(src):
        c = src[j]
        if c == '"':
            j += 1
            while src[j] != '"':
                j += 2 if src[j] == "\\" else 1
        elif c in pairs:
            stack.append(pairs[c])
        elif c in ")}]":
            if c != stack.pop():
                raise ValueError("unbalanced brackets")
            if not stack:
                return j
        j += 1
    raise ValueError("unterminated bracket")


def split_top(s, sep):
    """Split s on sep at bracket depth 0 (strings respected)."""
    parts, depth, cur, i = [], 0, [], 0
    while i < len(s):
        c = s[i]
        if c == '"':
            j = i + 1
            while s[j] != '"':
                j += 2 if s[j] == "\\" else 1
            cur.append(s[i:j + 1]); i = j + 1; continue
        if c in "({[":
            depth += 1
        elif c in ")}]":
            depth -= 1
        if c == sep and depth == 0:
            parts.append("".join(cur).strip()); cur = []
        else:
            cur.append(c)
        i += 1
    tail = "".join(cur).strip()
    if tail:
        parts.append(tail)
    return parts


# --------------------------------------------------------------------------
# Expression evaluation (numbers, booleans, arithmetic, content references)

class Ref:
    """Reference to another piece of content, e.g. Items.copper."""
    def __init__(self, kind, field):
        self.kind, self.field = kind, field
    def __repr__(self):
        return "%s.%s" % (self.kind, self.field)


class Unknown(Exception):
    pass


_num = re.compile(r"(?<![\w.])(\d+\.\d*|\.\d+|\d+)([fFdDlL])\b")


def java_to_py(expr):
    e = _num.sub(r"\1", expr.strip())
    e = re.sub(r"\btrue\b", "True", e)
    e = re.sub(r"\bfalse\b", "False", e)
    e = re.sub(r"\(\s*(float|int|double)\s*\)", "", e)  # casts
    e = re.sub(r"\bwith\(", "_with(", e)
    e = e.replace("&&", " and ").replace("||", " or ")
    e = re.sub(r"!(?!=)", " not ", e)
    return e


def evaluate(expr, env):
    try:
        tree = ast.parse(java_to_py(expr), mode="eval")
    except SyntaxError:
        raise Unknown(expr)
    return _ev(tree.body, env)


def _ev(n, env):
    if isinstance(n, ast.Constant):
        return n.value
    if isinstance(n, ast.Name):
        if n.id in env:
            return env[n.id]
        raise Unknown(n.id)
    if isinstance(n, ast.Attribute) and isinstance(n.value, ast.Name):
        if n.value.id in ("Items", "Liquids", "UnitTypes", "Blocks", "StatusEffects"):
            return Ref(n.value.id, n.attr)
        if n.value.id in ("Category", "BuildVisibility", "Env", "BlockGroup"):
            return n.attr
        if n.value.id in env and isinstance(env[n.value.id], dict) and n.attr in env[n.value.id]:
            return env[n.value.id][n.attr]
        raise Unknown(ast.dump(n))
    if isinstance(n, ast.UnaryOp):
        v = _ev(n.operand, env)
        if isinstance(n.op, ast.USub): return -v
        if isinstance(n.op, ast.UAdd): return v
        if isinstance(n.op, ast.Not): return not v
    if isinstance(n, ast.BinOp):
        a, b = _ev(n.left, env), _ev(n.right, env)
        if isinstance(n.op, ast.Add): return a + b
        if isinstance(n.op, ast.Sub): return a - b
        if isinstance(n.op, ast.Mult): return a * b
        if isinstance(n.op, ast.Div):
            return a // b if isinstance(a, int) and isinstance(b, int) else a / b
        if isinstance(n.op, ast.BitXor): return a ^ b
        if isinstance(n.op, ast.BitOr): return a | b
    if isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id == "_with":
        args = [_ev(a, env) for a in n.args]
        return [(args[i], args[i + 1]) for i in range(0, len(args), 2)]
    raise Unknown(ast.dump(n))


# --------------------------------------------------------------------------
# Class defaults (field initialisers of Block, Drill, UnitType ...)

class ClassIndex:
    def __init__(self, src_root):
        self.files = {}
        for d, _, fs in os.walk(src_root):
            for f in fs:
                if f.endswith(".java"):
                    self.files.setdefault(f[:-5], os.path.join(d, f))
        self.cache = {}

    def info(self, cls):
        if cls in self.cache:
            return self.cache[cls]
        path = self.files.get(cls)
        if not path:
            self.cache[cls] = None
            return None
        src = strip_comments(open(path, encoding="utf-8").read())
        m = re.search(r"\bclass\s+%s\b(?:<[^>{]*>)?\s*(?:extends\s+(\w+))?" % cls, src)
        parent = m.group(1) if m else None
        fields = {}
        for fm in re.finditer(r"^ {4}public\s+(?:@\w+\s+)*(%s)\s+([^;(]+?);" % "|".join(PRIMS), src, re.M):
            typ = fm.group(1)
            for decl in split_top(fm.group(2), ","):
                if "=" in decl:
                    k, v = decl.split("=", 1)
                    k = k.strip()
                    try:
                        val = evaluate(v, {})
                    except Unknown:
                        val = None
                else:
                    k, val = decl.strip(), (False if typ == "boolean" else 0)
                if re.fullmatch(r"\w+", k):
                    if typ in ("int", "byte", "short", "long") and isinstance(val, float):
                        val = int(val)
                    fields[k] = val
        # constructor statements (e.g. Conveyor: rotate = true; itemCapacity = capacity)
        ctor = []
        cm = re.search(r"^ {4}public\s+%s\s*\(\s*String\s+\w+\s*\)\s*\{" % cls, src, re.M)
        if cm:
            ob = cm.end() - 1
            ctor = split_top(src[ob + 1:match_brace(src, ob)], ";")
        res = (parent, fields, ctor)
        self.cache[cls] = res
        return res

    def defaults(self, cls):
        chain = []
        while cls:
            inf = self.info(cls)
            if not inf:
                break
            chain.append((cls, inf[1], inf[2]))
            cls = inf[0]
        out = {}
        for _, f, ctor in reversed(chain):
            out.update(f)
            for k, v in assignments(ctor, out):
                if k in out and not isinstance(v, str):
                    out[k] = v
        return out, [c[0] for c in chain]


# --------------------------------------------------------------------------
# Content definitions

DEF_RE = re.compile(r"^ {8}(\w+)\s*=\s*new\s+(\w+)\(", re.M)


class Def:
    def __init__(self, field, cls, args, body):
        self.field, self.cls, self.args, self.body = field, cls, args, body
        self.statements = split_top(body, ";") if body else []
        m = re.match(r'\s*"([^"]+)"', args)
        self.name = m.group(1) if m else None
        if self.name is None and cls == "OreBlock":
            # OreBlock(Item ore) names itself "ore-" + item.name
            m = re.match(r"\s*Items\.(\w+)", args)
            self.ore_item_field = m.group(1) if m else None

    def __repr__(self):
        return "Def(%s %s %s)" % (self.field, self.cls, self.name)


def parse_defs(path):
    src = strip_comments(open(path, encoding="utf-8").read())
    defs = []
    for m in DEF_RE.finditer(src):
        po = m.end() - 1
        pc = match_brace(src, po)
        args = src[po + 1:pc]
        rest = src[pc + 1:pc + 8].lstrip()
        body = ""
        if rest.startswith("{{"):
            start = src.index("{{", pc)
            end = match_brace(src, start)
            body = src[start + 2:end - 1]
        defs.append(Def(m.group(1), m.group(2), args, body))
    return defs


def parse_inner_new(expr):
    """Parse `new Cls(args){{ body }}` -> (cls, [args], body) or None."""
    m = re.match(r"\s*new\s+(\w+)\s*\(", expr)
    if not m:
        return None
    po = m.end() - 1
    pc = match_brace(expr, po)
    args = split_top(expr[po + 1:pc], ",")
    rest = expr[pc + 1:].strip()
    body = ""
    if rest.startswith("{{"):
        bc = match_brace(rest, 0)
        body = rest[2:bc - 1]
    return m.group(1), args, body


def assignments(statements, env):
    """Yield (key, value) for simple `a = b = expr` statements."""
    for st in statements:
        m = re.match(r"^([\w.]+(?:\s*=\s*[\w.]+)*)\s*=\s*(.+)$", st, re.S)
        if not m or "==" in st.split("=", 1)[0] or st.lstrip().startswith(("if", "for")):
            continue
        targets = [t.strip() for t in re.split(r"\s*=\s*", m.group(1))]
        rhs = m.group(2).strip()
        try:
            val = evaluate(rhs, env)
        except Unknown:
            val = rhs  # keep raw text, caller decides
        for t in targets:
            yield t, val
