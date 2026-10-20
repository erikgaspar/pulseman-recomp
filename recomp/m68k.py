"""Decodificador de 68000 que gera C para cada instrução.

decode(rom, addr) devolve um Insn com:
  text   - desmontagem legível (sintaxe Motorola)
  size   - tamanho em bytes
  flow   - 'next' | 'cond' | 'jump' | 'call' | 'ret' | 'ijump' | 'icall' | 'end' | 'illegal'
  targets- destinos estáticos conhecidos (branches, chamadas)
  code   - linhas de C que executam a instrução (podem conter 'return')
"""

import struct


class Invalid(Exception):
    pass


SZ_NAME = {1: 'b', 2: 'w', 4: 'l'}
CC_NAMES = ['t', 'f', 'hi', 'ls', 'cc', 'cs', 'ne', 'eq', 'vc', 'vs', 'pl', 'mi', 'ge', 'lt', 'gt', 'le']


def s8(v):
    return v - 0x100 if v & 0x80 else v


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def u32(v):
    return v & 0xFFFFFFFF


def hx(v):
    return '0x%X' % u32(v)


def mask(sz):
    return {1: 0xFF, 2: 0xFFFF, 4: 0xFFFFFFFF}[sz]


class EA:
    """Operando decodificado. kind: dn an ind postinc predec disp idx absw absl pcdisp pcidx imm"""

    def __init__(self, kind, reg=0, disp=0, addr=0, idx=None, value=0):
        self.kind = kind
        self.reg = reg
        self.disp = disp
        self.addr = addr      # endereço resolvido (abs, pc-relativo)
        self.idx = idx        # (is_a, reg, is_long)
        self.value = value    # imediato

    def is_mem(self):
        return self.kind not in ('dn', 'an', 'imm')

    def text(self, sz=2):
        k = self.kind
        if k == 'dn':
            return 'd%d' % self.reg
        if k == 'an':
            return 'a%d' % self.reg if self.reg != 7 else 'sp'
        r = 'a%d' % self.reg if self.reg != 7 else 'sp'
        if k == 'ind':
            return '(%s)' % r
        if k == 'postinc':
            return '(%s)+' % r
        if k == 'predec':
            return '-(%s)' % r
        if k == 'disp':
            return '%d(%s)' % (self.disp, r)
        if k == 'idx':
            return '%d(%s,%s)' % (self.disp, r, self.idx_text())
        if k == 'absw':
            return '($%X).w' % (self.addr & 0xFFFF)
        if k == 'absl':
            return '($%X).l' % self.addr
        if k == 'pcdisp':
            return '$%X(pc)' % self.addr
        if k == 'pcidx':
            return '$%X(pc,%s)' % (self.addr, self.idx_text())
        if k == 'imm':
            return '#$%X' % self.value
        return '?'

    def idx_text(self):
        is_a, r, is_long = self.idx
        return '%s%d.%s' % ('a' if is_a else 'd', r, 'l' if is_long else 'w')

    def idx_c(self):
        is_a, r, is_long = self.idx
        reg = 'A(%d)' % r if is_a else 'D(%d)' % r
        return reg if is_long else 'SX16(%s)' % reg


def ea_ok(mode, reg, cls):
    """Valida o modo de endereçamento para a classe exigida pela instrução."""
    if mode == 7 and reg > 4:
        return False
    is_dn = mode == 0
    is_an = mode == 1
    is_pc = mode == 7 and reg in (2, 3)
    is_imm = mode == 7 and reg == 4
    if cls == 'all':
        return True
    if cls == 'data':
        return not is_an
    if cls == 'dalt':
        return not (is_an or is_pc or is_imm)
    if cls == 'malt':
        return not (is_dn or is_an or is_pc or is_imm)
    if cls == 'alt':
        return not (is_pc or is_imm)
    if cls == 'ctrl':
        return mode in (2, 5, 6) or (mode == 7 and reg in (0, 1, 2, 3))
    if cls == 'dnopc':  # btst estático: dados, sem imediato
        return not (is_an or is_imm)
    raise ValueError(cls)


class Ctx:
    def __init__(self):
        self.lines = []
        self.ntmp = 0

    def emit(self, s):
        self.lines.append(s)

    def tmp(self, prefix='t'):
        self.ntmp += 1
        return '%s%d' % (prefix, self.ntmp)


class Operand:
    """Acesso a um operando EA dentro do C gerado."""

    def __init__(self, ctx, ea, sz):
        self.ctx = ctx
        self.ea = ea
        self.sz = sz
        self.var = None

    def addr_expr(self):
        """Expressão C do endereço efetivo (sem efeitos colaterais; não serve para (An)+/-(An))."""
        e = self.ea
        k = e.kind
        if k == 'ind':
            return 'A(%d)' % e.reg
        if k == 'disp':
            return 'A(%d) %s %d' % (e.reg, '-' if e.disp < 0 else '+', abs(e.disp))
        if k == 'idx':
            d = '' if e.disp == 0 else ' %s %d' % ('-' if e.disp < 0 else '+', abs(e.disp))
            return 'A(%d)%s + %s' % (e.reg, d, e.idx_c())
        if k in ('absw', 'absl', 'pcdisp'):
            return hx(e.addr)
        if k == 'pcidx':
            return '%s + %s' % (hx(e.addr), e.idx_c())
        raise Invalid('sem endereço: ' + k)

    def prep(self):
        """Calcula o endereço (aplicando pós-incremento/pré-decremento)."""
        e = self.ea
        if not e.is_mem():
            return self
        v = self.ctx.tmp('ea')
        self.var = v
        inc = self.sz
        if e.reg == 7 and self.sz == 1:
            inc = 2
        if e.kind == 'postinc':
            self.ctx.emit('uint32_t %s = A(%d); A(%d) += %d;' % (v, e.reg, e.reg, inc))
        elif e.kind == 'predec':
            self.ctx.emit('A(%d) -= %d; uint32_t %s = A(%d);' % (e.reg, inc, v, e.reg))
        else:
            self.ctx.emit('uint32_t %s = %s;' % (v, self.addr_expr()))
        return self

    def read(self):
        e = self.ea
        sz = self.sz
        if e.kind == 'dn':
            return 'D(%d)' % e.reg if sz == 4 else '(D(%d) & 0x%X)' % (e.reg, mask(sz))
        if e.kind == 'an':
            return 'A(%d)' % e.reg if sz == 4 else '(A(%d) & 0x%X)' % (e.reg, mask(sz))
        if e.kind == 'imm':
            return hx(e.value & mask(sz))
        return 'rd%d(%s)' % (sz * 8, self.var)

    def write(self, val):
        e = self.ea
        sz = self.sz
        if e.kind == 'dn':
            self.ctx.emit('SETD%d(%d, %s);' % (sz * 8, e.reg, val))
        elif e.kind == 'an':
            self.ctx.emit('A(%d) = %s;' % (e.reg, val))
        elif e.is_mem():
            self.ctx.emit('wr%d(%s, %s);' % (sz * 8, self.var, val))
        else:
            raise Invalid('escrita em imediato')


def ea_cycles(ea, sz):
    """Estimativa de ciclos para acessar o operando (4 por ciclo de barramento)."""
    if ea is None:
        return 0
    k = ea.kind
    lw = 4 if sz == 4 else 0
    return {
        'dn': 0, 'an': 0, 'ind': 4 + lw, 'postinc': 4 + lw, 'predec': 6 + lw,
        'disp': 8 + lw, 'idx': 10 + lw, 'absw': 8 + lw, 'absl': 12 + lw,
        'pcdisp': 8 + lw, 'pcidx': 10 + lw, 'imm': 4 + lw,
    }[k]


class Insn:
    def __init__(self, addr):
        self.addr = addr
        self.size = 2
        self.text = ''
        self.flow = 'next'
        self.targets = []
        self.code = []
        self.cycles = 4
        self.ends_block = False   # força fim de bloco (ex.: mudou SR)
        self.eas = []             # operandos na ordem de decodificação (para heurísticas)

    @property
    def mn(self):
        return self.text.split(' ', 1)[0]

    @property
    def next(self):
        return self.addr + self.size


class Decoder:
    def __init__(self, rom):
        self.rom = rom

    def w(self, a):
        if a + 2 > len(self.rom):
            raise Invalid('fora da ROM')
        return struct.unpack_from('>H', self.rom, a)[0]

    def fetch16(self):
        v = self.w(self.p)
        self.p += 2
        return v

    def fetch32(self):
        v = (self.w(self.p) << 16) | self.w(self.p + 2)
        self.p += 4
        return v

    def parse_ea(self, mode, reg, sz, cls='all'):
        ea = self._parse_ea(mode, reg, sz, cls)
        self.ins.eas.append(ea)
        return ea

    def _parse_ea(self, mode, reg, sz, cls):
        if not ea_ok(mode, reg, cls):
            raise Invalid('modo %d/%d inválido p/ %s' % (mode, reg, cls))
        if mode == 0:
            return EA('dn', reg)
        if mode == 1:
            if sz == 1:
                raise Invalid('An em byte')
            return EA('an', reg)
        if mode == 2:
            return EA('ind', reg)
        if mode == 3:
            return EA('postinc', reg)
        if mode == 4:
            return EA('predec', reg)
        if mode == 5:
            return EA('disp', reg, disp=s16(self.fetch16()))
        if mode == 6:
            ext = self.fetch16()
            return EA('idx', reg, disp=s8(ext & 0xFF), idx=(bool(ext & 0x8000), (ext >> 12) & 7, bool(ext & 0x800)))
        if reg == 0:
            return EA('absw', addr=u32(s16(self.fetch16())))
        if reg == 1:
            return EA('absl', addr=self.fetch32())
        if reg == 2:
            base = self.p
            return EA('pcdisp', addr=u32(base + s16(self.fetch16())))
        if reg == 3:
            base = self.p
            ext = self.fetch16()
            return EA('pcidx', addr=u32(base + s8(ext & 0xFF)), idx=(bool(ext & 0x8000), (ext >> 12) & 7, bool(ext & 0x800)))
        if reg == 4:
            if sz == 1:
                return EA('imm', value=self.fetch16() & 0xFF)
            if sz == 2:
                return EA('imm', value=self.fetch16())
            return EA('imm', value=self.fetch32())
        raise Invalid('modo 7/%d' % reg)

    def ea_src(self, op, sz, cls='all'):
        return self.parse_ea((op >> 3) & 7, op & 7, sz, cls)

    def decode(self, addr):
        ins = Insn(addr)
        self.p = addr + 2
        self.ins = ins
        self.ctx = Ctx()
        try:
            op = self.w(addr)
            self.op = op
            handler = getattr(self, 'grp%X' % (op >> 12))
            handler(op)
        except Invalid as ex:
            ins.flow = 'illegal'
            ins.size = 2
            ins.text = 'dc.w $%04X ; inválido (%s)' % (self.w(addr) if addr + 2 <= len(self.rom) else 0, ex)
            ins.code = ['return cpu_exception(4, %s);' % hx(addr)]
            ins.cycles = 34
            return ins
        ins.size = self.p - addr
        ins.code = self.ctx.lines
        return ins

    # ---------- utilidades de emissão ----------

    def E(self, s):
        self.ctx.emit(s)

    def opnd(self, ea, sz):
        return Operand(self.ctx, ea, sz)

    def set_text(self, mn, *ops):
        self.ins.text = mn + (' ' + ','.join(ops) if ops else '')

    def cyc(self, n):
        self.ins.cycles = n

    def priv(self):
        self.E('if (!cpu.s) return cpu_exception(8, %s);' % hx(self.ins.addr))

    def illegal(self, why='opcode'):
        raise Invalid(why)

    # ---------- grupo 0: bits, imediatos, movep ----------

    def grp0(self, op):
        mode = (op >> 3) & 7
        if op & 0x100:
            if mode == 1:
                return self.movep(op)
            return self.bitop(op, dynamic=True)
        sub = (op >> 9) & 7
        if sub == 4:
            return self.bitop(op, dynamic=False)
        if sub == 7:
            self.illegal()
        names = {0: 'ori', 1: 'andi', 2: 'subi', 3: 'addi', 5: 'eori', 6: 'cmpi'}
        name = names.get(sub)
        sz = {0: 1, 1: 2, 2: 4}.get((op >> 6) & 3)
        if name is None or sz is None:
            self.illegal()
        if (op & 0x3F) == 0x3C and name in ('ori', 'andi', 'eori'):
            if sz == 4:
                self.illegal()
            imm = self.fetch16()
            cop = {'ori': '|', 'andi': '&', 'eori': '^'}[name]
            if sz == 1:
                self.set_text(name, '#$%X' % (imm & 0xFF), 'ccr')
                self.E('set_ccr(get_ccr() %s 0x%X);' % (cop, imm & 0xFF))
                self.cyc(20)
            else:
                self.set_text(name, '#$%X' % imm, 'sr')
                self.priv()
                self.E('cpu_set_sr(cpu_get_sr() %s 0x%X);' % (cop, imm))
                self.ins.ends_block = True
                self.cyc(20)
            return
        imm = self.parse_ea(7, 4, sz)
        dst = self.ea_src(op, sz, 'dalt')
        self.set_text('%s.%s' % (name, SZ_NAME[sz]), imm.text(sz), dst.text(sz))
        d = self.opnd(dst, sz).prep()
        iv = hx(imm.value & mask(sz))
        if name == 'cmpi':
            self.E('op_cmp(%s, %s, %d);' % (d.read(), iv, sz))
        elif name in ('subi', 'addi'):
            d.write('op_%s(%s, %s, %d)' % (name[:3], d.read(), iv, sz))
        else:
            cop = {'ori': '|', 'andi': '&', 'eori': '^'}[name]
            d.write('op_logic(%s %s %s, %d)' % (d.read(), cop, iv, sz))
        base = 8 if sz != 4 else 16
        if dst.is_mem():
            base += 4
        self.cyc(base + ea_cycles(dst, sz) * (1 if name == 'cmpi' else 2))

    def bitop(self, op, dynamic):
        typ = (op >> 6) & 3
        name = ['btst', 'bchg', 'bclr', 'bset'][typ]
        if dynamic:
            bitsrc = 'd%d' % ((op >> 9) & 7)
            bitexpr = 'D(%d)' % ((op >> 9) & 7)
        else:
            imm = self.fetch16() & 0xFF
            bitsrc = '#%d' % imm
            bitexpr = str(imm)
        mode = (op >> 3) & 7
        if mode == 0:
            sz = 4
        else:
            sz = 1
        if typ == 0:
            cls = 'data' if dynamic else 'dnopc'
        else:
            cls = 'dalt'
        dst = self.ea_src(op, sz, cls)
        self.set_text(name, bitsrc, dst.text(sz))
        d = self.opnd(dst, sz).prep()
        b = self.ctx.tmp('b')
        self.E('uint32_t %s = (%s) & %d;' % (b, bitexpr, 31 if sz == 4 else 7))
        v = self.ctx.tmp('v')
        self.E('uint32_t %s = %s;' % (v, d.read()))
        self.E('cpu.z = ((%s >> %s) & 1) == 0;' % (v, b))
        if typ == 1:
            d.write('%s ^ (1u << %s)' % (v, b))
        elif typ == 2:
            d.write('%s & ~(1u << %s)' % (v, b))
        elif typ == 3:
            d.write('%s | (1u << %s)' % (v, b))
        self.cyc((6 if typ == 0 else 8) + (0 if dynamic else 4) + ea_cycles(dst, sz) * (1 if typ == 0 else 2))

    def movep(self, op):
        dreg = (op >> 9) & 7
        areg = op & 7
        opm = (op >> 6) & 3
        disp = s16(self.fetch16())
        sz = 4 if opm & 1 else 2
        mem = '%d(a%d)' % (disp, areg)
        a = self.ctx.tmp('ea')
        self.E('uint32_t %s = A(%d) %s %d;' % (a, areg, '-' if disp < 0 else '+', abs(disp)))
        n = sz
        if opm & 2:
            self.set_text('movep.%s' % SZ_NAME[sz], 'd%d' % dreg, mem)
            for i in range(n):
                sh = (n - 1 - i) * 8
                self.E('wr8(%s + %d, (D(%d) >> %d) & 0xFF);' % (a, i * 2, dreg, sh))
        else:
            self.set_text('movep.%s' % SZ_NAME[sz], mem, 'd%d' % dreg)
            parts = ['(rd8(%s + %d) << %d)' % (a, i * 2, (n - 1 - i) * 8) for i in range(n)]
            self.E('SETD%d(%d, %s);' % (sz * 8, dreg, ' | '.join(parts)))
        self.cyc(16 if sz == 2 else 24)

    # ---------- grupos 1-3: move ----------

    def grp_move(self, op, sz):
        src = self.ea_src(op, sz, 'all')
        dmode = (op >> 6) & 7
        dreg = (op >> 9) & 7
        if dmode == 1:
            if sz == 1:
                self.illegal()
            self.set_text('movea.%s' % SZ_NAME[sz], src.text(sz), 'a%d' % dreg)
            s = self.opnd(src, sz).prep()
            val = s.read() if sz == 4 else 'SX16(%s)' % s.read()
            self.E('A(%d) = %s;' % (dreg, val))
            self.cyc(4 + ea_cycles(src, sz))
            return
        dst = self.parse_ea(dmode, dreg, sz, 'dalt')
        self.set_text('move.%s' % SZ_NAME[sz], src.text(sz), dst.text(sz))
        s = self.opnd(src, sz).prep()
        v = self.ctx.tmp('v')
        self.E('uint32_t %s = op_logic(%s, %d);' % (v, s.read(), sz))
        d = self.opnd(dst, sz).prep()
        if sz == 4 and dst.kind == 'predec':
            # o 68000 grava a palavra baixa primeiro em move.l para -(An)
            self.E('wr16(%s + 2, %s & 0xFFFF); wr16(%s, %s >> 16);' % (d.var, v, d.var, v))
        else:
            d.write(v)
        self.cyc(4 + ea_cycles(src, sz) + ea_cycles(dst, sz) - (2 if dst.kind == 'predec' else 0))

    def grp1(self, op):
        self.grp_move(op, 1)

    def grp2(self, op):
        self.grp_move(op, 4)

    def grp3(self, op):
        self.grp_move(op, 2)

    # ---------- grupo 4: diversos ----------

    def grp4(self, op):
        ins = self.ins
        if op == 0x4AFC:
            ins.text = 'illegal'
            ins.flow = 'illegal'
            self.E('return cpu_exception(4, %s);' % hx(ins.addr))
            self.cyc(34)
            return
        if (op & 0xFFF0) == 0x4E40:
            n = op & 15
            self.set_text('trap', '#%d' % n)
            ins.flow = 'end'
            self.E('return cpu_exception(%d, %s);' % (32 + n, hx(ins.addr + 2)))
            self.cyc(34)
            return
        if (op & 0xFFF8) == 0x4E50:
            r = op & 7
            d = s16(self.fetch16())
            self.set_text('link', 'a%d' % r, '#%d' % d)
            if r == 7:
                self.E('A(7) -= 4; wr32(A(7), A(7));')
            else:
                self.E('push32(A(%d)); A(%d) = A(7);' % (r, r))
            self.E('A(7) += %d;' % d if d >= 0 else 'A(7) -= %d;' % -d)
            self.cyc(16)
            return
        if (op & 0xFFF8) == 0x4E58:
            r = op & 7
            self.set_text('unlk', 'a%d' % r)
            self.E('A(7) = A(%d); A(%d) = pop32();' % (r, r) if r != 7 else 'A(7) = rd32(A(7));')
            self.cyc(12)
            return
        if (op & 0xFFF0) == 0x4E60:
            r = op & 7
            self.priv()
            if op & 8:
                self.set_text('move', 'usp', 'a%d' % r)
                self.E('A(%d) = cpu.other_sp;' % r)
            else:
                self.set_text('move', 'a%d' % r, 'usp')
                self.E('cpu.other_sp = A(%d);' % r)
            self.cyc(4)
            return
        if op == 0x4E70:
            ins.text = 'reset'
            self.priv()
            self.cyc(132)
            return
        if op == 0x4E71:
            ins.text = 'nop'
            return
        if op == 0x4E72:
            imm = self.fetch16()
            self.set_text('stop', '#$%04X' % imm)
            self.priv()
            ins.flow = 'end'
            self.E('return cpu_stop(0x%X, %s);' % (imm, hx(ins.addr + 4)))
            return
        if op == 0x4E73:
            ins.text = 'rte'
            ins.flow = 'ret'
            self.priv()
            self.E('return cpu_rte();')
            self.cyc(20)
            return
        if op == 0x4E75:
            ins.text = 'rts'
            ins.flow = 'ret'
            self.E('return pop32();')
            self.cyc(16)
            return
        if op == 0x4E76:
            ins.text = 'trapv'
            self.E('if (cpu.v) return cpu_exception(7, %s);' % hx(ins.addr + 2))
            return
        if op == 0x4E77:
            ins.text = 'rtr'
            ins.flow = 'ret'
            self.E('set_ccr(pop16());')
            self.E('return pop32();')
            self.cyc(20)
            return
        if (op & 0xFF80) == 0x4E80:
            is_jmp = op & 0x40
            ea = self.ea_src(op, 4, 'ctrl')
            self.set_text('jmp' if is_jmp else 'jsr', ea.text())
            o = self.opnd(ea, 4)
            static = ea.kind in ('absw', 'absl', 'pcdisp')
            t = self.ctx.tmp('t')
            self.E('uint32_t %s = %s;' % (t, o.addr_expr()))
            if not is_jmp:
                self.E('push32(%s);' % hx(self.p))
            self.E('return %s;' % t)
            if static:
                ins.targets.append(ea.addr & 0xFFFFFF)
                ins.flow = 'jump' if is_jmp else 'call'
            else:
                ins.flow = 'ijump' if is_jmp else 'icall'
            self.cyc((8 if is_jmp else 16) + ea_cycles(ea, 2))
            return
        if (op & 0xFFB8) == 0x4880:
            r = op & 7
            if op & 0x40:
                self.set_text('ext.l', 'd%d' % r)
                self.E('D(%d) = op_logic(SX16(D(%d)), 4);' % (r, r))
            else:
                self.set_text('ext.w', 'd%d' % r)
                self.E('SETD16(%d, op_logic(SX8(D(%d)), 2));' % (r, r))
            return
        if (op & 0xFB80) == 0x4880:
            return self.movem(op)
        if (op & 0xF1C0) == 0x41C0:
            r = (op >> 9) & 7
            ea = self.ea_src(op, 4, 'ctrl')
            self.set_text('lea', ea.text(), 'a%d' % r)
            self.E('A(%d) = %s;' % (r, self.opnd(ea, 4).addr_expr()))
            self.cyc(4 + ea_cycles(ea, 2) - 4)
            return
        if (op & 0xF1C0) == 0x4180:
            r = (op >> 9) & 7
            ea = self.ea_src(op, 2, 'data')
            self.set_text('chk.w', ea.text(2), 'd%d' % r)
            o = self.opnd(ea, 2).prep()
            sv, bv = self.ctx.tmp('s'), self.ctx.tmp('b')
            self.E('int32_t %s = (int16_t)D(%d), %s = (int16_t)%s;' % (sv, r, bv, o.read()))
            self.E('cpu.z = %s == 0; cpu.v = cpu.c = 0;' % sv)
            self.E('if (%s < 0 || %s > %s) { cpu.n = %s < 0; return cpu_exception(6, %s); }'
                   % (sv, sv, bv, sv, hx(self.p)))
            self.cyc(10 + ea_cycles(ea, 2))
            return
        if (op & 0xFFC0) == 0x40C0:
            ea = self.ea_src(op, 2, 'dalt')
            self.set_text('move', 'sr', ea.text(2))
            o = self.opnd(ea, 2).prep()
            o.write('cpu_get_sr()')
            self.cyc(6 + ea_cycles(ea, 2))
            return
        if (op & 0xFFC0) == 0x44C0:
            ea = self.ea_src(op, 2, 'data')
            self.set_text('move', ea.text(2), 'ccr')
            o = self.opnd(ea, 2).prep()
            self.E('set_ccr(%s);' % o.read())
            self.cyc(12 + ea_cycles(ea, 2))
            return
        if (op & 0xFFC0) == 0x46C0:
            ea = self.ea_src(op, 2, 'data')
            self.set_text('move', ea.text(2), 'sr')
            self.priv()
            o = self.opnd(ea, 2).prep()
            self.E('cpu_set_sr(%s);' % o.read())
            ins.ends_block = True
            self.cyc(12 + ea_cycles(ea, 2))
            return
        if (op & 0xFFC0) == 0x4800:
            ea = self.ea_src(op, 1, 'dalt')
            self.set_text('nbcd', ea.text(1))
            o = self.opnd(ea, 1).prep()
            w, r = self.ctx.tmp('w'), self.ctx.tmp('r')
            self.E('int %s; uint32_t %s = op_nbcd(%s, &%s);' % (w, r, o.read(), w))
            self.E('if (%s) {' % w)
            o.write(r)
            self.E('}')
            self.cyc(6 + ea_cycles(ea, 1) * 2)
            return
        if (op & 0xFFF8) == 0x4840:
            r = op & 7
            self.set_text('swap', 'd%d' % r)
            self.E('D(%d) = op_logic((D(%d) >> 16) | (D(%d) << 16), 4);' % (r, r, r))
            return
        if (op & 0xFFC0) == 0x4840:
            ea = self.ea_src(op, 4, 'ctrl')
            self.set_text('pea', ea.text())
            t = self.ctx.tmp('t')
            self.E('uint32_t %s = %s; push32(%s);' % (t, self.opnd(ea, 4).addr_expr(), t))
            self.cyc(12 + ea_cycles(ea, 2) - 4)
            return
        if (op & 0xFFC0) == 0x4AC0:
            ea = self.ea_src(op, 1, 'dalt')
            self.set_text('tas', ea.text(1))
            o = self.opnd(ea, 1).prep()
            v = self.ctx.tmp('v')
            self.E('uint32_t %s = op_logic(%s, 1);' % (v, o.read()))
            o.write('%s | 0x80' % v)
            self.cyc(14 + ea_cycles(ea, 1))
            return
        sz = {0: 1, 1: 2, 2: 4}.get((op >> 6) & 3)
        top = op & 0xFF00
        if sz and top in (0x4000, 0x4200, 0x4400, 0x4600, 0x4A00):
            name = {0x4000: 'negx', 0x4200: 'clr', 0x4400: 'neg', 0x4600: 'not', 0x4A00: 'tst'}[top]
            ea = self.ea_src(op, sz, 'dalt')
            self.set_text('%s.%s' % (name, SZ_NAME[sz]), ea.text(sz))
            o = self.opnd(ea, sz).prep()
            if name == 'clr':
                o.write('op_logic(0, %d)' % sz)
            elif name == 'tst':
                self.E('op_logic(%s, %d);' % (o.read(), sz))
            elif name == 'neg':
                o.write('op_sub(0, %s, %d)' % (o.read(), sz))
            elif name == 'negx':
                o.write('op_subx(0, %s, %d)' % (o.read(), sz))
            else:
                o.write('op_logic(~%s, %d)' % (o.read(), sz))
            mult = 1 if name == 'tst' else 2
            self.cyc((4 if sz != 4 else 6) + ea_cycles(ea, sz) * mult)
            return
        if (op & 0xFF00) == 0x4C00 or (op & 0xFF00) == 0x4800:
            self.illegal()
        self.illegal()

    def movem(self, op):
        to_regs = bool(op & 0x400)
        sz = 4 if op & 0x40 else 2
        mask_w = self.fetch16()
        mode = (op >> 3) & 7
        if to_regs:
            if mode in (0, 1, 4) or (mode == 7 and (op & 7) == 4):
                self.illegal()
            ea = self.ea_src(op, sz, 'all')
        else:
            if mode in (0, 1, 3):
                self.illegal()
            ea = self.ea_src(op, sz, 'alt')
        regs = []
        if ea.kind == 'predec':
            for i in range(16):
                if mask_w & (1 << i):
                    regs.append(15 - i)
        else:
            for i in range(16):
                if mask_w & (1 << i):
                    regs.append(i)
        names = ['%s%d' % ('d' if r < 8 else 'a', r & 7) for r in sorted(regs)]
        rl = '/'.join(names) if names else '0'
        if to_regs:
            self.set_text('movem.%s' % SZ_NAME[sz], ea.text(sz), rl)
        else:
            self.set_text('movem.%s' % SZ_NAME[sz], rl, ea.text(sz))

        def reg_c(r):
            return 'D(%d)' % r if r < 8 else 'A(%d)' % (r - 8)

        a = self.ctx.tmp('ea')
        if ea.kind == 'predec':
            # ordem: a7..a0, d7..d0, descendo
            self.E('uint32_t %s = A(%d);' % (a, ea.reg))
            for r in regs:
                if sz == 4:
                    # como no move.l -(An): palavra baixa primeiro
                    self.E('%s -= 4; wr16(%s + 2, %s & 0xFFFF); wr16(%s, %s >> 16);'
                           % (a, a, reg_c(r), a, reg_c(r)))
                else:
                    self.E('%s -= 2; wr16(%s, %s);' % (a, a, reg_c(r)))
            self.E('A(%d) = %s;' % (ea.reg, a))
        else:
            if ea.kind == 'postinc':
                self.E('uint32_t %s = A(%d);' % (a, ea.reg))
            else:
                self.E('uint32_t %s = %s;' % (a, self.opnd(ea, sz).addr_expr()))
            for r in regs:
                if to_regs:
                    val = 'rd32(%s)' % a if sz == 4 else 'SX16(rd16(%s))' % a
                    self.E('%s = %s; %s += %d;' % (reg_c(r), val, a, sz))
                else:
                    self.E('wr%d(%s, %s); %s += %d;' % (sz * 8, a, reg_c(r), a, sz))
            if ea.kind == 'postinc':
                self.E('A(%d) = %s;' % (ea.reg, a))
        self.cyc(8 + len(regs) * (8 if sz == 4 else 4) + ea_cycles(ea, 2))

    # ---------- grupo 5: addq/subq/scc/dbcc ----------

    def grp5(self, op):
        ins = self.ins
        szb = (op >> 6) & 3
        if szb == 3:
            cc = (op >> 8) & 15
            mode = (op >> 3) & 7
            if mode == 1:
                r = op & 7
                base = self.p
                disp = s16(self.fetch16())
                target = u32(base + disp) & 0xFFFFFF
                self.set_text('db%s' % ('ra' if cc == 1 else CC_NAMES[cc]), 'd%d' % r, '$%X' % target)
                ins.flow = 'cond'
                ins.targets.append(target)
                cond = 'CC_%s' % CC_NAMES[cc].upper()
                self.E('if (!(%s)) {' % cond)
                self.E('    uint32_t w = (D(%d) - 1) & 0xFFFF; SETD16(%d, w);' % (r, r))
                self.E('    if (w != 0xFFFF) { CYC(10); return %s; }' % hx(target))
                self.E('    CYC(14);')
                self.E('} else CYC(12);')
                self.cyc(0)
                return
            ea = self.ea_src(op, 1, 'dalt')
            self.set_text('s%s' % CC_NAMES[cc], ea.text(1))
            o = self.opnd(ea, 1).prep()
            o.write('(CC_%s) ? 0xFF : 0' % CC_NAMES[cc].upper())
            self.cyc(6 + ea_cycles(ea, 1) * 2)
            return
        sz = {0: 1, 1: 2, 2: 4}[szb]
        data = (op >> 9) & 7 or 8
        is_sub = op & 0x100
        ea = self.ea_src(op, sz, 'alt')
        name = 'subq' if is_sub else 'addq'
        self.set_text('%s.%s' % (name, SZ_NAME[sz]), '#%d' % data, ea.text(sz))
        if ea.kind == 'an':
            self.E('A(%d) %s= %d;' % (ea.reg, '-' if is_sub else '+', data))
            self.cyc(8)
            return
        o = self.opnd(ea, sz).prep()
        o.write('op_%s(%s, %d, %d)' % ('sub' if is_sub else 'add', o.read(), data, sz))
        self.cyc((4 if sz != 4 else 8) + ea_cycles(ea, sz) * 2)

    # ---------- grupo 6: bcc/bra/bsr ----------

    def grp6(self, op):
        ins = self.ins
        cc = (op >> 8) & 15
        disp = s8(op & 0xFF)
        base = ins.addr + 2
        suffix = '.s'
        if disp == 0:
            disp = s16(self.fetch16())
            suffix = '.w'
        target = u32(base + disp) & 0xFFFFFF
        name = {0: 'bra', 1: 'bsr'}.get(cc, 'b' + CC_NAMES[cc])
        self.set_text(name + suffix, '$%X' % target)
        ins.targets.append(target)
        if cc == 0:
            ins.flow = 'jump'
            self.E('return %s;' % hx(target))
            self.cyc(10)
        elif cc == 1:
            ins.flow = 'call'
            self.E('push32(%s);' % hx(self.p))
            self.E('return %s;' % hx(target))
            self.cyc(18)
        else:
            ins.flow = 'cond'
            self.E('if (CC_%s) { CYC(2); return %s; }' % (CC_NAMES[cc].upper(), hx(target)))
            self.cyc(8)

    # ---------- grupo 7: moveq ----------

    def grp7(self, op):
        if op & 0x100:
            self.illegal()
        r = (op >> 9) & 7
        v = s8(op & 0xFF)
        self.set_text('moveq', '#%d' % v, 'd%d' % r)
        self.E('D(%d) = op_logic(%s, 4);' % (r, hx(v)))

    # ---------- grupos 8/C: or/and/div/mul/bcd/exg ----------

    def grp8(self, op):
        self.or_and(op, 'or')

    def grpC(self, op):
        if (op & 0x1F8) in (0x140, 0x148, 0x188):
            x = (op >> 9) & 7
            y = op & 7
            kind = op & 0x1F8
            if kind == 0x140:
                rx, ry = 'D(%d)' % x, 'D(%d)' % y
                self.set_text('exg', 'd%d' % x, 'd%d' % y)
            elif kind == 0x148:
                rx, ry = 'A(%d)' % x, 'A(%d)' % y
                self.set_text('exg', 'a%d' % x, 'a%d' % y)
            else:
                rx, ry = 'D(%d)' % x, 'A(%d)' % y
                self.set_text('exg', 'd%d' % x, 'a%d' % y)
            self.E('uint32_t t = %s; %s = %s; %s = t;' % (rx, rx, ry, ry))
            self.cyc(6)
            return
        self.or_and(op, 'and')

    def or_and(self, op, name):
        r = (op >> 9) & 7
        opm = (op >> 6) & 7
        if opm in (3, 7):
            signed = opm == 7
            ea = self.ea_src(op, 2, 'data')
            if name == 'or':
                mn = 'divs' if signed else 'divu'
                self.set_text(mn + '.w', ea.text(2), 'd%d' % r)
                o = self.opnd(ea, 2).prep()
                self.E('if (op_%s(&D(%d), %s)) return cpu_exception(5, %s);' % (mn, r, o.read(), hx(self.p)))
                self.cyc((158 if signed else 140) + ea_cycles(ea, 2))
            else:
                mn = 'muls' if signed else 'mulu'
                self.set_text(mn + '.w', ea.text(2), 'd%d' % r)
                o = self.opnd(ea, 2).prep()
                self.E('D(%d) = op_%s(D(%d), %s);' % (r, mn, r, o.read()))
                self.cyc(54 + ea_cycles(ea, 2))
            return
        if (op & 0x1F0) == 0x100:
            mn = 'sbcd' if name == 'or' else 'abcd'
            y = op & 7
            if op & 8:
                self.set_text(mn, '-(a%d)' % y, '-(a%d)' % r)
                self.bcd_mem(mn, y, r)
            else:
                self.set_text(mn, 'd%d' % y, 'd%d' % r)
                self.E('SETD8(%d, op_%s(D(%d) & 0xFF, D(%d) & 0xFF));' % (r, mn, r, y))
                self.cyc(6)
            return
        sz = {0: 1, 1: 2, 2: 4, 4: 1, 5: 2, 6: 4}[opm]
        cop = '|' if name == 'or' else '&'
        if opm < 4:
            ea = self.ea_src(op, sz, 'data')
            self.set_text('%s.%s' % (name, SZ_NAME[sz]), ea.text(sz), 'd%d' % r)
            o = self.opnd(ea, sz).prep()
            self.E('SETD%d(%d, op_logic(D(%d) %s %s, %d));' % (sz * 8, r, r, cop, o.read(), sz))
            self.cyc((4 if sz != 4 else 6) + ea_cycles(ea, sz))
        else:
            ea = self.ea_src(op, sz, 'malt')
            self.set_text('%s.%s' % (name, SZ_NAME[sz]), 'd%d' % r, ea.text(sz))
            o = self.opnd(ea, sz).prep()
            o.write('op_logic(%s %s D(%d), %d)' % (o.read(), cop, r, sz))
            self.cyc((8 if sz != 4 else 12) + ea_cycles(ea, sz) * 2)

    def bcd_mem(self, mn, y, x):
        # Fonte primeiro (-(Ay)), depois destino (-(Ax)); A7 decrementa 2 em byte
        iy = 2 if y == 7 else 1
        ix = 2 if x == 7 else 1
        self.E('A(%d) -= %d; uint32_t s = rd8(A(%d));' % (y, iy, y))
        self.E('A(%d) -= %d; uint32_t ea = A(%d); uint32_t d = rd8(ea);' % (x, ix, x))
        self.E('wr8(ea, op_%s(d, s));' % mn)
        self.cyc(18)

    # ---------- grupos 9/D: sub/add ----------

    def grp9(self, op):
        self.add_sub(op, 'sub')

    def grpD(self, op):
        self.add_sub(op, 'add')

    def add_sub(self, op, name):
        r = (op >> 9) & 7
        opm = (op >> 6) & 7
        if opm in (3, 7):
            sz = 2 if opm == 3 else 4
            ea = self.ea_src(op, sz, 'all')
            self.set_text('%sa.%s' % (name, SZ_NAME[sz]), ea.text(sz), 'a%d' % r)
            o = self.opnd(ea, sz).prep()
            val = o.read() if sz == 4 else 'SX16(%s)' % o.read()
            self.E('A(%d) %s= %s;' % (r, '+' if name == 'add' else '-', val))
            self.cyc(8 + ea_cycles(ea, sz))
            return
        sz = {0: 1, 1: 2, 2: 4, 4: 1, 5: 2, 6: 4}[opm]
        if opm >= 4 and ((op >> 4) & 3) == 0:
            y = op & 7
            mn = name + 'x'
            if op & 8:
                self.set_text('%s.%s' % (mn, SZ_NAME[sz]), '-(a%d)' % y, '-(a%d)' % r)
                iy = 2 if (y == 7 and sz == 1) else sz
                ix = 2 if (r == 7 and sz == 1) else sz
                bits = sz * 8
                self.E('A(%d) -= %d; uint32_t s = rd%d(A(%d));' % (y, iy, bits, y))
                self.E('A(%d) -= %d; uint32_t ea = A(%d); uint32_t d = rd%d(ea);' % (r, ix, r, bits))
                self.E('wr%d(ea, op_%s(d, s, %d));' % (bits, mn, sz))
                self.cyc(18 if sz != 4 else 30)
            else:
                self.set_text('%s.%s' % (mn, SZ_NAME[sz]), 'd%d' % y, 'd%d' % r)
                self.E('SETD%d(%d, op_%s(D(%d), D(%d), %d));' % (sz * 8, r, mn, r, y, sz))
                self.cyc(4 if sz != 4 else 8)
            return
        if opm < 4:
            ea = self.ea_src(op, sz, 'all')
            self.set_text('%s.%s' % (name, SZ_NAME[sz]), ea.text(sz), 'd%d' % r)
            o = self.opnd(ea, sz).prep()
            self.E('SETD%d(%d, op_%s(D(%d), %s, %d));' % (sz * 8, r, name, r, o.read(), sz))
            self.cyc((4 if sz != 4 else 6) + ea_cycles(ea, sz))
        else:
            ea = self.ea_src(op, sz, 'malt')
            self.set_text('%s.%s' % (name, SZ_NAME[sz]), 'd%d' % r, ea.text(sz))
            o = self.opnd(ea, sz).prep()
            o.write('op_%s(%s, D(%d), %d)' % (name, o.read(), r, sz))
            self.cyc((8 if sz != 4 else 12) + ea_cycles(ea, sz) * 2)

    # ---------- grupo B: cmp/cmpa/cmpm/eor ----------

    def grpB(self, op):
        r = (op >> 9) & 7
        opm = (op >> 6) & 7
        if opm in (3, 7):
            sz = 2 if opm == 3 else 4
            ea = self.ea_src(op, sz, 'all')
            self.set_text('cmpa.%s' % SZ_NAME[sz], ea.text(sz), 'a%d' % r)
            o = self.opnd(ea, sz).prep()
            val = o.read() if sz == 4 else 'SX16(%s)' % o.read()
            self.E('op_cmp(A(%d), %s, 4);' % (r, val))
            self.cyc(6 + ea_cycles(ea, sz))
            return
        sz = {0: 1, 1: 2, 2: 4, 4: 1, 5: 2, 6: 4}[opm]
        if opm < 4:
            ea = self.ea_src(op, sz, 'all')
            self.set_text('cmp.%s' % SZ_NAME[sz], ea.text(sz), 'd%d' % r)
            o = self.opnd(ea, sz).prep()
            self.E('op_cmp(D(%d), %s, %d);' % (r, o.read(), sz))
            self.cyc((4 if sz != 4 else 6) + ea_cycles(ea, sz))
            return
        if ((op >> 3) & 7) == 1:
            y = op & 7
            self.set_text('cmpm.%s' % SZ_NAME[sz], '(a%d)+' % y, '(a%d)+' % r)
            iy = 2 if (y == 7 and sz == 1) else sz
            ix = 2 if (r == 7 and sz == 1) else sz
            bits = sz * 8
            self.E('uint32_t s = rd%d(A(%d)); A(%d) += %d;' % (bits, y, y, iy))
            self.E('uint32_t d = rd%d(A(%d)); A(%d) += %d;' % (bits, r, r, ix))
            self.E('op_cmp(d, s, %d);' % sz)
            self.cyc(12 if sz != 4 else 20)
            return
        ea = self.ea_src(op, sz, 'dalt')
        self.set_text('eor.%s' % SZ_NAME[sz], 'd%d' % r, ea.text(sz))
        o = self.opnd(ea, sz).prep()
        o.write('op_logic(%s ^ D(%d), %d)' % (o.read(), r, sz))
        self.cyc((4 if sz != 4 else 8) + ea_cycles(ea, sz) * 2)

    # ---------- grupo E: deslocamentos ----------

    def grpE(self, op):
        names = ['as', 'ls', 'rox', 'ro']
        szb = (op >> 6) & 3
        left = bool(op & 0x100)
        if szb == 3:
            if op & 0x800:
                self.illegal()
            typ = (op >> 9) & 3
            ea = self.ea_src(op, 2, 'malt')
            mn = names[typ] + ('l' if left else 'r')
            self.set_text(mn + '.w', ea.text(2))
            o = self.opnd(ea, 2).prep()
            o.write('op_%s(%s, 1, 2)' % (mn, o.read()))
            self.cyc(8 + ea_cycles(ea, 2) * 2)
            return
        sz = {0: 1, 1: 2, 2: 4}[szb]
        typ = (op >> 3) & 3
        r = op & 7
        mn = names[typ] + ('l' if left else 'r')
        cnt = (op >> 9) & 7
        if op & 0x20:
            self.set_text('%s.%s' % (mn, SZ_NAME[sz]), 'd%d' % cnt, 'd%d' % r)
            cexpr = '(D(%d) & 63)' % cnt
        else:
            n = cnt or 8
            self.set_text('%s.%s' % (mn, SZ_NAME[sz]), '#%d' % n, 'd%d' % r)
            cexpr = str(n)
        self.E('SETD%d(%d, op_%s(D(%d), %s, %d));' % (sz * 8, r, mn, r, cexpr, sz))
        self.cyc((6 if sz != 4 else 8) + (2 * (cnt or 8) if not (op & 0x20) else 8))

    # ---------- linhas A e F ----------

    def grpA(self, op):
        self.ins.text = 'dc.w $%04X ; line-A' % op
        self.ins.flow = 'illegal'
        self.E('return cpu_exception(10, %s);' % hx(self.ins.addr))

    def grpF(self, op):
        self.ins.text = 'dc.w $%04X ; line-F' % op
        self.ins.flow = 'illegal'
        self.E('return cpu_exception(11, %s);' % hx(self.ins.addr))
