#!/usr/bin/env python3
"""
eel2cpp - translates a JSFX file (EEL2) into a C++ class that reproduces the arithmetic of REAPER's
EEL2 compiler: grammar and associativity of eel2.y, fuzzy == and truth tests (0.00001), 64-bit integer
& | ~, int % << >>, memory indexing with +0.00001, loop()/while() limits, the denormal/NaN filter on stores,
left-to-right evaluation of operands, sqrt(|x|), min/max picking rules, and the compiler's constant
optimizations that change results (x^0, x^1, e^x -> exp).

The generated code is not edited by hand: change the JSFX, run this script, build.

usage: eel2cpp.py <input.jsfx> <out_dir> <ClassName>
"""
import re, sys, os, json

# ----------------------------------------------------------------------------------------------- lexer
TOK = re.compile(r'''
   (?P<ws>\s+)
 | (?P<cmt>//[^\n]*|/\*.*?\*/)
 | (?P<str>"(?:\\.|[^"\\])*")
 | (?P<chr>'(?:\\.|[^'\\]){1,4}')
 | (?P<hex>0x[0-9A-Fa-f]+)
 | (?P<num>(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)
 | (?P<dollar>\$(?:pi|e|phi|x[0-9A-Fa-f]+|'.'))
 | (?P<sname>\#[A-Za-z_0-9.]*)
 | (?P<id>[A-Za-z_][A-Za-z_0-9.]*)
 | (?P<op>===|!==|==|!=|<=|>=|<<|>>|&&|\|\||\+=|-=|\*=|/=|%=|\^=|\|=|&=|~=|[-+*/%^&|~!<>=?:;,()\[\]])
''', re.S | re.X)

class Tok:
    __slots__ = ('k', 'v', 'line')
    def __init__(s, k, v, line): s.k, s.v, s.line = k, v, line
    def __repr__(s): return f'{s.k}:{s.v}@{s.line}'

def lex(src, line0):
    out, pos, line = [], 0, line0
    while pos < len(src):
        m = TOK.match(src, pos)
        if not m: raise SyntaxError(f'line {line}: cannot read {src[pos:pos+30]!r}')
        k = m.lastgroup; v = m.group(k)
        if k not in ('ws', 'cmt'):
            out.append(Tok(k, v, line))
        line += v.count('\n'); pos = m.end()
    out.append(Tok('eof', '', line))
    return out

# ----------------------------------------------------------------------------------------------- AST
class N:
    def __init__(s, t, *a, **kw):
        s.t = t; s.a = list(a); s.__dict__.update(kw)
    def __repr__(s): return f'{s.t}{s.a}'

ASSIGN_OPS = {'=', '+=', '-=', '*=', '/=', '%=', '^=', '|=', '&=', '~='}

class Parser:
    def __init__(s, toks): s.t, s.i = toks, 0
    def peek(s, o=0): return s.t[s.i + o]
    def take(s):
        t = s.t[s.i]; s.i += 1; return t
    def isop(s, *v): return s.peek().k == 'op' and s.peek().v in v
    def expect(s, v):
        t = s.take()
        if t.v != v: raise SyntaxError(f'line {t.line}: expected {v!r}, got {t.v!r}')
        return t

    # expression: if_else (';' if_else)*  [';']
    def expression(s, stop=(')', ',', ']')):
        items = []
        while True:
            if s.peek().k == 'eof' or s.isop(*stop): break
            if s.peek().k == 'id' and s.peek().v.lower() == 'function':
                items.append(s.funcdef());
                if s.isop(';'): s.take()
                continue
            items.append(s.if_else())
            if s.isop(';'):
                s.take(); continue
            break
        if not items: return N('num', 0.0)
        return items[0] if len(items) == 1 else N('seq', *items)

    def funcdef(s):
        s.take(); name = s.take().v.lower()
        s.expect('('); params = []
        while not s.isop(')'):
            params.append(s.take().v.lower())
            if s.isop(','): s.take()
        s.expect(')')
        locs = []
        while s.peek().k == 'id' and s.peek().v.lower() in ('local', 'instance', 'globals', 'global'):
            kind = s.take().v.lower(); s.expect('(')
            while not s.isop(')'):
                v = s.take().v.lower()
                if kind == 'local': locs.append(v)
                elif kind == 'instance': raise SyntaxError('instance() is not supported')
                if s.isop(','): s.take()
            s.expect(')')
        s.expect('(')
        body = s.expression()
        s.expect(')')
        return N('func', name=name, params=params, locs=locs, body=body)

    def if_else(s):
        c = s.logical()
        if s.isop('?'):
            s.take()
            if s.isop(':'):
                s.take(); b = s.if_else(); return N('if', c, None, b)
            a = s.if_else()
            if s.isop(':'):
                s.take(); b = s.if_else(); return N('if', c, a, b)
            return N('if', c, a, None)
        return c

    def binlevel(s, sub, ops):
        l = sub()
        while s.isop(*ops):
            op = s.take().v; r = sub(); l = N('bin', l, r, op=op)
        return l
    def logical(s): return s.binlevel(s.cmp, ('&&', '||'))
    def cmp(s): return s.binlevel(s.andor, ('<', '>', '<=', '>=', '==', '===', '!=', '!=='))
    def andor(s): return s.binlevel(s.add, ('&', '|', '~'))
    def add(s): return s.binlevel(s.sub, ('+',))
    def sub(s): return s.binlevel(s.mul, ('-',))
    def mul(s): return s.binlevel(s.div, ('*',))
    def div(s): return s.binlevel(s.mod, ('/',))
    def mod(s): return s.binlevel(s.pow, ('%', '<<', '>>'))
    def pow(s): return s.binlevel(s.unary, ('^',))
    def unary(s):
        if s.isop('+'): s.take(); return s.unary()
        if s.isop('-'): s.take(); return N('neg', s.unary())
        if s.isop('!'): s.take(); return N('not', s.unary())
        return s.assignment()

    def assignment(s):
        lhs = s.postfix()
        if s.peek().k == 'op' and s.peek().v in ASSIGN_OPS:
            op = s.take().v; rhs = s.if_else()
            if lhs.t not in ('var', 'mem', 'sname'):
                raise SyntaxError(f'line {s.peek().line}: cannot assign to {lhs.t}')
            return N('asg', lhs, rhs, op=op)
        return lhs

    def postfix(s):
        e = s.primary()
        while s.isop('['):
            s.take()
            idx = None if s.isop(']') else s.expression(stop=(']',))
            s.expect(']'); e = N('mem', e, idx)
        return e

    def primary(s):
        t = s.take()
        if t.k == 'num': return N('num', float(t.v), txt=t.v)
        if t.k == 'hex': return N('num', float(int(t.v, 16)), txt=t.v)
        if t.k == 'chr':
            body = bytes(t.v[1:-1], 'utf-8').decode('unicode_escape'); v = 0
            for ch in body: v = v * 256 + ord(ch)
            return N('num', float(v), txt=t.v)
        if t.k == 'dollar':
            d = t.v[1:]
            import math
            v = {'pi': math.pi, 'e': math.e, 'phi': 1.618033988749894848}.get(d)
            if v is None and d.startswith('x'): v = float(int(d[1:], 16))
            if v is None and d.startswith("'"): v = float(ord(d[1]))
            return N('num', v, txt=repr(v))
        if t.k == 'str':
            txt = t.v[1:-1]
            while s.peek().k == 'str': txt += s.take().v[1:-1]
            return N('strlit', txt)
        if t.k == 'sname': return N('sname', t.v[1:].lower())
        if t.k == 'id':
            name = t.v.lower()
            if s.isop('('):
                s.take(); args = []
                if not s.isop(')'):
                    while True:
                        args.append(s.expression(stop=(')', ',')))
                        if s.isop(','): s.take(); continue
                        break
                s.expect(')')
                if name == 'while' and s.isop('('):          # while (cond) (body)
                    s.take(); body = s.expression(); s.expect(')')
                    return N('while2', args[0], body)
                return N('call', *args, name=name)
            return N('var', name)
        if t.k == 'op' and t.v == '(':
            e = s.expression(); s.expect(')'); return e
        raise SyntaxError(f'line {t.line}: unexpected {t.v!r}')

# ----------------------------------------------------------------------------------------------- JSFX file
def read_jsfx(path):
    src = open(path, encoding='utf-8').read()
    lines = src.split('\n')
    header, sections, cur, start = [], {}, None, 0
    for i, l in enumerate(lines):
        m = re.match(r'@(\w+)(.*)$', l)
        if m:
            if cur: sections[cur] = ('\n'.join(lines[start:i]), start + 1)
            cur = m.group(1); start = i + 1
            if cur == 'gfx': sections['gfx_size'] = m.group(2).split()
        elif cur is None:
            header.append(l)
    if cur: sections[cur] = ('\n'.join(lines[start:]), start + 1)
    return header, sections

def parse_sliders(header):
    out = []
    for l in header:
        m = re.match(r'slider(\d+):([^<]*)<([^>]*)>(.*)$', l.strip())
        if not m: continue
        idx = int(m.group(1)); default = float(m.group(2))
        rng = m.group(3); label = m.group(4); hidden = label.startswith('-'); label = label.lstrip('-')
        enum = None
        em = re.match(r'([^,]*),([^,]*),([^,{]*)(?:\{(.*)\})?', rng)
        lo, hi, step = float(em.group(1)), float(em.group(2)), float(em.group(3) or 0)
        if em.group(4) is not None: enum = em.group(4).split(',')
        out.append(dict(idx=idx, default=default, lo=lo, hi=hi, step=step, enum=enum, label=label, hidden=hidden))
    return out

def header_value(header, key):
    for l in header:
        m = re.match(r'(?://)?' + key + r':\s*(.*)$', l.strip())
        if m: return m.group(1).strip()
    return ''

# ----------------------------------------------------------------------------------------------- emitter
BUILTIN_PURE = {'floor', 'ceil', 'abs', 'sqrt', 'exp', 'log', 'log10', 'sin', 'cos', 'tan', 'atan', 'atan2',
                'min', 'max', 'sqr', 'sign', 'pow'}
BUILTIN_EFFECT = {'memset', 'memcpy', 'fft', 'ifft', 'fft_permute', 'fft_ipermute', 'slider_automate',
                  'file_var', 'file_mem', 'file_avail', 'strcpy', 'strcat', 'sprintf', 'strlen', 'str_getchar',
                  'strcpy_substr', 'match', 'time_precise', 'gfx_rect', 'gfx_line', 'gfx_circle', 'gfx_arc',
                  'gfx_roundrect', 'gfx_triangle', 'gfx_drawstr', 'gfx_setfont', 'gfx_measurestr', 'gfx_getchar'}
BYREF = {'gfx_measurestr': (1, 2), 'match': (2, 3, 4, 5, 6, 7), 'file_var': (1,)}

def cnum(v):
    if v != v: return 'NAN'
    if v in (float('inf'), float('-inf')): return 'INFINITY' if v > 0 else '-INFINITY'
    r = repr(float(v))
    if 'e' not in r and '.' not in r: r += '.0'
    return r

class Emitter:
    def __init__(s, cls):
        s.cls = cls; s.globals = set(); s.funcs = {}; s.strings = {}; s.snames = {}
        s.locals_of = {}; s.cur = None; s.tmpn = 0
    # ---- names
    def strid(s, txt):
        if txt not in s.strings: s.strings[txt] = 90000 + len(s.strings)
        return s.strings[txt]
    def snid(s, name):
        if name not in s.snames: s.snames[name] = 80000 + len(s.snames)
        return s.snames[name]
    def varname(s, name):
        if s.cur and name in s.locals_of[s.cur]: return f'l_{s.cur}_{cid(name)}'
        s.globals.add(name); return f'v_{cid(name)}'
    def tmp(s): s.tmpn += 1; return f'_t{s.tmpn}'

    # ---- side effects (anything that writes a variable, memory or state)
    def effect(s, n):
        if n is None: return False
        if n.t in ('asg', 'while2', 'func'): return True
        if n.t == 'call':
            if n.name in BUILTIN_PURE: return any(s.effect(a) for a in n.a)
            if n.name in ('loop',): return True
            return True
        if n.t == 'seq': return any(s.effect(a) for a in n.a)
        return any(s.effect(a) for a in n.a if isinstance(a, N))
    # ---- what a node reads (for ordering decisions it is enough to know "reads anything")
    def needs_stmt(s, n):
        return n is not None and (n.t in ('while2',) or (n.t == 'call' and n.name in ('loop', 'while')) or
                                  (n.t == 'seq' and any(s.needs_stmt(x) for x in n.a)) or
                                  (n.t == 'if' and (s.needs_stmt(n.a[1]) or s.needs_stmt(n.a[2]))) or
                                  (n.t not in ('seq', 'if') and any(s.needs_stmt(x) for x in n.a if isinstance(x, N))))

    # ---- statements
    def stmts(s, n, out, ind):
        p = '  ' * ind
        if n is None: return
        if n.t == 'seq':
            for x in n.a:
                if x.t == 'func': continue
                s.stmts(x, out, ind)
            return
        if n.t == 'func': return
        if n.t == 'if':
            c, a, b = n.a
            out.append(f'{p}if (T({s.expr(c)})) {{')
            s.stmts(a, out, ind + 1)
            if b is not None:
                out.append(f'{p}}} else {{'); s.stmts(b, out, ind + 1)
            out.append(f'{p}}}'); return
        if n.t == 'bin' and n.op in ('&&', '||') and s.effect(n.a[1]):
            # short-circuit with an action on the right: as an if
            cond = f'T({s.expr(n.a[0])})' if n.op == '&&' else f'!T({s.expr(n.a[0])})'
            out.append(f'{p}if ({cond}) {{'); s.stmts(n.a[1], out, ind + 1); out.append(f'{p}}}'); return
        if n.t == 'call' and n.name == 'loop':
            c = s.tmp()
            out.append(f'{p}for (int {c} = loopcount({s.expr(n.a[0])}); {c} > 0; --{c}) {{')
            s.stmts(n.a[1], out, ind + 1); out.append(f'{p}}}'); return
        if n.t == 'while2':
            c = s.tmp()
            out.append(f'{p}for (int {c} = 1048576; {c} > 0 && T({s.expr(n.a[0])}); --{c}) {{')
            s.stmts(n.a[1], out, ind + 1); out.append(f'{p}}}'); return
        if n.t == 'call' and n.name == 'while':
            c = s.tmp()
            out.append(f'{p}for (int {c} = 1048576; {c} > 0; --{c}) {{')
            body = n.a[0]
            items = body.a if body.t == 'seq' else [body]
            for x in items[:-1]: s.stmts(x, out, ind + 1)
            out.append(f'{p}  if (!T({s.expr(items[-1])})) break;')
            out.append(f'{p}}}'); return
        if n.t in ('num', 'var', 'strlit', 'sname'): return          # a bare value does nothing
        out.append(f'{p}(void)({s.expr(n)});')

    # ---- expressions (C++ expression of type double)
    def lam(s, n):
        out = []
        items = n.a if n.t == 'seq' else [n]
        items = [x for x in items if x.t != 'func']
        for x in items[:-1]: s.stmts(x, out, 1)
        last = items[-1] if items else N('num', 0.0)
        if s.needs_stmt(last):
            r = s.tmp(); out.append(f'  double {r} = 0.0;')
            s.valstmts(last, r, out, 1)
            out.append(f'  return {r};')
        else:
            out.append(f'  return {s.expr(last)};')
        return '([&]() -> double {\n' + '\n'.join(out) + '\n}())'

    def valstmts(s, n, r, out, ind):
        """statements that leave the value of n in r"""
        p = '  ' * ind
        if n.t == 'if':
            c, a, b = n.a
            out.append(f'{p}if (T({s.expr(c)})) {{')
            if a is not None: s.valstmts(a, r, out, ind + 1)
            else: out.append(f'{p}  {r} = 0.0;')
            out.append(f'{p}}} else {{')
            if b is not None: s.valstmts(b, r, out, ind + 1)
            else: out.append(f'{p}  {r} = 0.0;')
            out.append(f'{p}}}'); return
        if n.t == 'seq':
            items = [x for x in n.a if x.t != 'func']
            for x in items[:-1]: s.stmts(x, out, ind)
            s.valstmts(items[-1], r, out, ind); return
        if n.t == 'call' and n.name == 'loop':
            c = s.tmp()
            out.append(f'{p}for (int {c} = loopcount({s.expr(n.a[0])}); {c} > 0; --{c}) {{')
            s.valstmts(n.a[1], r, out, ind + 1); out.append(f'{p}}}'); return
        if n.t == 'while2':
            c = s.tmp()
            out.append(f'{p}for (int {c} = 1048576; {c} > 0 && T({s.expr(n.a[0])}); --{c}) {{')
            s.valstmts(n.a[1], r, out, ind + 1); out.append(f'{p}}}'); return
        out.append(f'{p}{r} = {s.expr(n)};')

    def addr(s, n):
        """address expression of a memory access: base + index as EEL adds them"""
        base, idx = n.a
        b = s.expr(base)
        if idx is None: return b
        i = s.expr(idx)
        if s.effect(idx) and s.reads(base):
            t = s.tmp(); return f'([&]() -> double {{ double {t} = {b}; return {t} + {i}; }}())'
        return f'({b} + {i})'

    def reads(s, n):
        return n is not None and n.t not in ('num', 'strlit')

    def lvalue(s, n):
        if n.t == 'var': return s.varname(n.a[0])
        if n.t == 'mem': return f'M({s.addr(n)})'
        raise SyntaxError('not assignable')

    def expr(s, n):
        t = n.t
        if t == 'num': return cnum(n.a[0])
        if t == 'var': return s.varname(n.a[0])
        if t == 'strlit': return cnum(s.strid(n.a[0]))
        if t == 'sname': return cnum(s.snid(n.a[0]))
        if t == 'mem': return f'M({s.addr(n)})'
        if t in ('seq', 'while2') or s.needs_stmt(n): return s.lam(n)
        if t == 'neg': return f'(-{s.expr(n.a[0])})'
        if t == 'not': return f'(T({s.expr(n.a[0])}) ? 0.0 : 1.0)'
        if t == 'if':
            c, a, b = n.a
            ea = s.expr(a) if a is not None else '0.0'
            eb = s.expr(b) if b is not None else '0.0'
            return f'(T({s.expr(c)}) ? {ea} : {eb})'
        if t == 'asg':
            lhs, rhs = n.a; op = n.op
            if lhs.t == 'sname':
                if op == '=': return f'str_copy({cnum(s.snid(lhs.a[0]))}, {s.expr(rhs)})'
                if op == '+=': return f'str_cat({cnum(s.snid(lhs.a[0]))}, {s.expr(rhs)})'
                raise SyntaxError('string op')
            r = s.expr(rhs)
            if lhs.t == 'var':
                lv = s.varname(lhs.a[0])
                if op == '=': return f'({lv} = DF({r}))'
                return s.compound(lv, op, r)
            # memory: address first, then the value
            a = s.addr(lhs); tv = s.tmp()
            if op == '=':
                if s.effect(rhs):
                    ta = s.tmp()
                    return f'([&]() -> double {{ double {ta} = {a}; double {tv} = {r}; return M({ta}) = DF({tv}); }}())'
                return f'(M({a}) = DF({r}))'
            ta = s.tmp()
            inner = s.compound(f'M({ta})', op, tv)
            return f'([&]() -> double {{ double {ta} = {a}; double {tv} = {r}; return {inner}; }}())'
        if t == 'bin':
            l, r = n.a; op = n.op
            if op == '&&': return f'((T({s.expr(l)}) && T({s.expr(r)})) ? 1.0 : 0.0)'
            if op == '||': return f'((T({s.expr(l)}) || T({s.expr(r)})) ? 1.0 : 0.0)'
            # constant rewrites of the EEL2 optimizer that change the result
            if op == '^' and r.t == 'num':
                if abs(r.a[0]) < 1e-30: return '1.0'
                if abs(r.a[0] - 1.0) < 1e-30: return s.expr(l)
            if op == '^' and l.t == 'num' and l.a[0] > 0:
                import math
                if abs(l.a[0] - 1.0) < 1e-30: return '1.0'
                if abs(math.log(l.a[0]) - 1.0) < 1e-9: return f'std::exp({s.expr(r)})'
            el = s.expr(l); er = s.expr(r)
            if s.effect(r) and s.reads(l):
                tl = s.tmp()
                return f'([&]() -> double {{ double {tl} = {el}; return {s.binop(op, tl, er)}; }}())'
            return s.binop(op, el, er)
        if t == 'call': return s.call(n)
        raise SyntaxError(f'cannot emit {t}')

    def compound(s, lv, op, r):
        if op == '+=': return f'({lv} = DF({lv} + {r}))'
        if op == '-=': return f'({lv} = DF({lv} - {r}))'
        if op == '*=': return f'({lv} = DF({lv} * {r}))'
        if op == '/=': return f'({lv} = DF({lv} / {r}))'
        if op == '%=': return f'({lv} = MOD({lv}, {r}))'
        if op == '^=': return f'({lv} = DF(std::pow({lv}, {r})))'
        if op == '|=': return f'({lv} = BOR({lv}, {r}))'
        if op == '&=': return f'({lv} = BAND({lv}, {r}))'
        if op == '~=': return f'({lv} = BXOR({lv}, {r}))'
        raise SyntaxError(op)

    def binop(s, op, a, b):
        return {'+': f'({a} + {b})', '-': f'({a} - {b})', '*': f'({a} * {b})', '/': f'({a} / {b})',
                '%': f'MOD({a}, {b})', '<<': f'SHL({a}, {b})', '>>': f'SHR({a}, {b})',
                '^': f'std::pow({a}, {b})', '&': f'BAND({a}, {b})', '|': f'BOR({a}, {b})', '~': f'BXOR({a}, {b})',
                '<': f'(({a}) < ({b}) ? 1.0 : 0.0)', '>': f'(({a}) > ({b}) ? 1.0 : 0.0)',
                '<=': f'(({a}) <= ({b}) ? 1.0 : 0.0)', '>=': f'(({a}) >= ({b}) ? 1.0 : 0.0)',
                '==': f'EQ({a}, {b})', '!=': f'NE({a}, {b})',
                '===': f'(({a}) == ({b}) ? 1.0 : 0.0)', '!==': f'(({a}) != ({b}) ? 1.0 : 0.0)'}[op]

    def args_in_order(s, exprs, effects):
        """evaluate arguments left to right when it matters; returns (prefix_lines, names)"""
        return exprs

    def call(s, n):
        name = n.name; a = n.a
        def E(i): return s.expr(a[i])
        if name in self_funcs_cache(s):
            f = s.funcs[name]
            ex = [s.expr(x) for x in a]
            while len(ex) < len(f.params): ex.append('0.0')
            if sum(1 for x in a if s.effect(x)) and len(a) > 1:
                ts = [s.tmp() for _ in ex]
                body = ' '.join(f'double {t} = {e};' for t, e in zip(ts, ex))
                return f'([&]() -> double {{ {body} return f_{cid(name)}({", ".join(ts)}); }}())'
            return f'f_{cid(name)}({", ".join(ex)})'
        if name == 'loop' or name == 'while': return s.lam(n)
        simple = {'floor': 'std::floor', 'ceil': 'std::ceil', 'abs': 'std::fabs', 'exp': 'std::exp',
                  'log': 'std::log', 'log10': 'std::log10', 'sin': 'std::sin', 'cos': 'std::cos', 'tan': 'std::tan',
                  'atan': 'std::atan'}
        if name in simple: return f'{simple[name]}({E(0)})'
        if name == 'sqrt': return f'std::sqrt(std::fabs({E(0)}))'
        if name == 'sqr':
            t = s.tmp(); return f'([&]() -> double {{ double {t} = {E(0)}; return {t} * {t}; }}())'
        if name == 'pow': return f'std::pow({E(0)}, {E(1)})'
        if name == 'atan2': return f'std::atan2({E(0)}, {E(1)})'
        if name == 'sign': return f'SIGN({E(0)})'
        if name in ('min', 'max'):
            ta, tb = s.tmp(), s.tmp()
            cmp = '>' if name == 'min' else '<'
            return f'([&]() -> double {{ double {ta} = {E(0)}; double {tb} = {E(1)}; return {ta} {cmp} {tb} ? {tb} : {ta}; }}())'
        if name == 'memset': return f'rt_memset({E(0)}, {E(1)}, {E(2)})'
        if name == 'memcpy': return f'rt_memcpy({E(0)}, {E(1)}, {E(2)})'
        if name in ('fft', 'ifft', 'fft_permute', 'fft_ipermute'):
            d = {'fft': 0, 'ifft': 1, 'fft_permute': 4, 'fft_ipermute': 5}[name]
            return f'rt_fft({d}, {E(0)}, {E(1)})'
        if name == 'slider_automate': return f'rt_slider_automate({E(0)}, {E(1) if len(a) > 1 else "0.0"})'
        if name == 'file_avail': return f'rt_file_avail({E(0)})'
        if name == 'file_var': return f'rt_file_var({E(0)}, &{s.lvalue(a[1])})'
        if name == 'file_mem': return f'rt_file_mem({E(0)}, {E(1)}, {E(2)})'
        if name == 'strcpy': return f'str_copy({E(0)}, {E(1)})'
        if name == 'strcat': return f'str_cat({E(0)}, {E(1)})'
        if name == 'strlen': return f'str_len({E(0)})'
        if name == 'str_getchar': return f'str_getchar({E(0)}, {E(1)})'
        if name == 'strcpy_substr': return f'str_copy_substr({E(0)}, {E(1)}, {E(2)}, {E(3) if len(a) > 3 else "1e30"})'
        if name == 'sprintf':
            rest = ', '.join(s.expr(x) for x in a[2:])
            return f'str_printf({E(0)}, {E(1)}, {{{rest}}})'
        if name == 'match':
            outs = ', '.join('&' + s.lvalue(x) for x in a[2:])
            return f'str_match({E(0)}, {E(1)}, {{{outs}}})'
        if name == 'time_precise': return 'rt_time_precise()'
        if name == 'gfx_rect': return f'g_rect({E(0)}, {E(1)}, {E(2)}, {E(3)}, {E(4) if len(a) > 4 else "1.0"})'
        if name == 'gfx_line': return f'g_line({E(0)}, {E(1)}, {E(2)}, {E(3)}, {E(4) if len(a) > 4 else "1.0"})'
        if name == 'gfx_circle': return f'g_circle({E(0)}, {E(1)}, {E(2)}, {E(3) if len(a) > 3 else "0.0"}, {E(4) if len(a) > 4 else "1.0"})'
        if name == 'gfx_arc': return f'g_arc({E(0)}, {E(1)}, {E(2)}, {E(3)}, {E(4)}, {E(5) if len(a) > 5 else "1.0"})'
        if name == 'gfx_roundrect': return f'g_roundrect({E(0)}, {E(1)}, {E(2)}, {E(3)}, {E(4)}, {E(5) if len(a) > 5 else "1.0"})'
        if name == 'gfx_triangle': return f'g_triangle({", ".join(s.expr(x) for x in a)})'
        if name == 'gfx_drawstr': return f'g_drawstr({E(0)})'
        if name == 'gfx_setfont':
            face = E(1) if len(a) > 1 else '-1.0'; sz = E(2) if len(a) > 2 else '0.0'; fl = E(3) if len(a) > 3 else '0.0'
            return f'g_setfont({E(0)}, {face}, {sz}, {fl})'
        if name == 'gfx_measurestr': return f'g_measurestr({E(0)}, &{s.lvalue(a[1])}, &{s.lvalue(a[2])})'
        if name == 'gfx_getchar': return 'g_getchar()'
        raise SyntaxError(f'unknown function {name}')

def self_funcs_cache(s): return s.funcs

def cid(name): return name.replace('.', '_')

# ----------------------------------------------------------------------------------------------- driver
def collect_funcs(node, out):
    if node is None: return
    if node.t == 'func': out.append(node); return
    for x in node.a:
        if isinstance(x, N): collect_funcs(x, out)

def main():
    src, outdir, cls = sys.argv[1], sys.argv[2], sys.argv[3]
    header, secs = read_jsfx(src)
    sliders = parse_sliders(header)
    em = Emitter(cls)
    trees = {}
    for name in ('init', 'serialize', 'block', 'sample', 'gfx'):
        if name not in secs: trees[name] = None; continue
        code, line0 = secs[name]
        trees[name] = Parser(lex(code, line0)).expression(stop=())
    # functions first, so calls resolve and locals are known
    allf = []
    for name, tr in trees.items(): collect_funcs(tr, allf)
    for f in allf:
        em.funcs[f.name] = f; em.locals_of[f.name] = set(f.params) | set(f.locs)
    fbodies = []
    for f in allf:
        em.cur = f.name
        out = []
        for p in f.params: out.append(f'  l_{f.name}_{cid(p)} = {cid(p)}_;')
        body = f.body
        items = [x for x in (body.a if body.t == 'seq' else [body]) if x.t != 'func']
        for x in items[:-1]: em.stmts(x, out, 1)
        last = items[-1] if items else N('num', 0.0)
        if em.needs_stmt(last):
            out.append('  double _r = 0.0;'); em.valstmts(last, '_r', out, 1); out.append('  return _r;')
        else:
            out.append(f'  return {em.expr(last)};')
        sig = ', '.join(f'double {cid(p)}_' for p in f.params)
        fbodies.append((f, sig, out))
        em.cur = None
    secbodies = {}
    for name, tr in trees.items():
        out = []
        if tr is not None: em.stmts(tr, out, 1)
        secbodies[name] = out

    gl = sorted(em.globals)
    locs = sorted({f'l_{f.name}_{cid(v)}' for f in allf for v in em.locals_of[f.name]})
    H = []
    H.append('// generated by tools/eel2cpp.py from ' + os.path.basename(src) + ' - do not edit, change the JSFX instead')
    H.append('#pragma once')
    H.append('#include "JsfxRuntime.h"')
    H.append('')
    H.append(f'class {cls} : public JsfxRuntime')
    H.append('{')
    H.append('public:')
    H.append(f'  {cls}();')
    H.append('  void sec_init() override;')
    H.append('  void sec_serialize() override;')
    H.append('  void sec_block() override;')
    H.append('  void sec_sample() override;')
    H.append('  void sec_gfx() override;')
    H.append('  double* var(const char* name) override;')
    H.append('  static const JsfxSliderInfo* sliderInfo(int& count);')
    H.append('  static const char* description();')
    H.append('  static int gfxWidth();')
    H.append('  static int gfxHeight();')
    H.append('')
    for g in gl: H.append(f'  double v_{cid(g)} = 0.0;')
    for l in locs: H.append(f'  double {l} = 0.0;')
    H.append('private:')
    for f, sig, _ in fbodies: H.append(f'  double f_{cid(f.name)}({sig});')
    H.append('};')

    C = []
    C.append('// generated by tools/eel2cpp.py from ' + os.path.basename(src) + ' - do not edit, change the JSFX instead')
    C.append(f'#include "{cls}.h"')
    C.append('#include <cmath>')
    C.append('#include <cstring>')
    C.append('')
    C.append('static const JsfxSliderInfo kSliders[] = {')
    for sl in sliders:
        enum = 'nullptr'
        if sl['enum']: enum = '"' + '|'.join(x.strip() for x in sl['enum']) + '"'
        lab = sl['label'].replace('"', '\\"')
        C.append(f'  {{ {sl["idx"]}, {cnum(sl["default"])}, {cnum(sl["lo"])}, {cnum(sl["hi"])}, {cnum(sl["step"])}, "{lab}", {enum}, {str(sl["hidden"]).lower()} }},')
    C.append('};')
    C.append(f'const JsfxSliderInfo* {cls}::sliderInfo(int& count) {{ count = {len(sliders)}; return kSliders; }}')
    desc = header_value(header, 'desc').replace('"', '\\"')
    C.append(f'const char* {cls}::description() {{ return "{desc}"; }}')
    gs = secs.get('gfx_size', ['0', '0'])
    C.append(f'int {cls}::gfxWidth() {{ return {int(gs[0]) if gs else 0}; }}')
    C.append(f'int {cls}::gfxHeight() {{ return {int(gs[1]) if len(gs) > 1 else 0}; }}')
    C.append('')
    C.append(f'{cls}::{cls}()')
    C.append('{')
    for txt, i in em.strings.items():
        lit = json.dumps(txt.encode('utf-8').decode('unicode_escape'), ensure_ascii=False)
        C.append(f'  str_define({i}, {lit});')
    for nm, i in em.snames.items(): C.append(f'  str_define({i}, "");')
    for sl in sliders: C.append(f'  v_slider{sl["idx"]} = {cnum(sl["default"])};') if f'slider{sl["idx"]}' in em.globals else None
    C.append('  bindVars();')
    C.append('}')
    C.append('')
    C.append(f'double* {cls}::var(const char* n)')
    C.append('{')
    for g in gl: C.append(f'  if (!std::strcmp(n, "{g}")) return &v_{cid(g)};')
    C.append('  return nullptr;')
    C.append('}')
    C.append('')
    for f, sig, out in fbodies:
        C.append(f'double {cls}::f_{cid(f.name)}({sig})')
        C.append('{'); C.extend(out); C.append('}'); C.append('')
    for name in ('init', 'serialize', 'block', 'sample', 'gfx'):
        C.append(f'void {cls}::sec_{name}()')
        C.append('{'); C.extend(secbodies[name]); C.append('}'); C.append('')
    os.makedirs(outdir, exist_ok=True)
    open(os.path.join(outdir, cls + '.h'), 'w').write('\n'.join(H) + '\n')
    open(os.path.join(outdir, cls + '.cpp'), 'w').write('\n'.join(C) + '\n')
    print(f'{cls}: {len(allf)} functions, {len(gl)} globals, {len(em.strings)} string literals, {len(em.snames)} named strings, {len(sliders)} sliders')

if __name__ == '__main__':
    main()
