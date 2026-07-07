#!/usr/bin/env python3

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path


DATA_BASE = 0x10000000
STACK_BASE = 0x7FFF0000
STEP_LIMIT = 1_000_000


REGISTER_ALIASES = {
    "zero": "x0",
    "ra": "x1",
    "sp": "x2",
    "gp": "x3",
    "tp": "x4",
    "t0": "x5",
    "t1": "x6",
    "t2": "x7",
    "s0": "x8",
    "fp": "x8",
    "s1": "x9",
    "a0": "x10",
    "a1": "x11",
    "a2": "x12",
    "a3": "x13",
    "a4": "x14",
    "a5": "x15",
    "a6": "x16",
    "a7": "x17",
    "s2": "x18",
    "s3": "x19",
    "s4": "x20",
    "s5": "x21",
    "s6": "x22",
    "s7": "x23",
    "s8": "x24",
    "s9": "x25",
    "s10": "x26",
    "s11": "x27",
    "t3": "x28",
    "t4": "x29",
    "t5": "x30",
    "t6": "x31",
}


def canon_reg(name: str) -> str:
    name = name.strip()
    return REGISTER_ALIASES.get(name, name)


def wrap32(value: int) -> int:
    return value & 0xFFFFFFFF


def signed32(value: int) -> int:
    value = wrap32(value)
    return value if value < 0x80000000 else value - 0x100000000


def trunc_div(lhs: int, rhs: int) -> int:
    if rhs == 0:
        raise RuntimeError("division by zero during assembly execution")
    return int(lhs / rhs)


@dataclass
class Instruction:
    op: str
    args: list[str]
    raw: str


class Program:
    def __init__(self, asm_path: Path) -> None:
        self.asm_path = asm_path
        self.data_labels: dict[str, int] = {}
        self.text_labels: dict[str, int] = {}
        self.memory: dict[int, int] = {}
        self.instructions: list[Instruction] = []
        self._parse()

    def _parse(self) -> None:
        section: str | None = None
        current_data_label: str | None = None
        next_data_addr = DATA_BASE

        for raw_line in self.asm_path.read_text(encoding="utf-8").splitlines():
            line = raw_line.split("#", 1)[0].strip()
            if not line:
                continue

            if line == ".data":
                section = "data"
                continue
            if line == ".text":
                section = "text"
                continue
            if line.startswith(".globl"):
                continue

            if line.endswith(":"):
                label = line[:-1]
                if section == "data":
                    self.data_labels[label] = next_data_addr
                    current_data_label = label
                elif section == "text":
                    self.text_labels[label] = len(self.instructions)
                else:
                    raise RuntimeError(f"label outside section: {label}")
                continue

            if section == "data":
                if not line.startswith(".word"):
                    raise RuntimeError(f"unsupported data directive: {line}")
                if current_data_label is None:
                    raise RuntimeError("data directive without label")
                value = int(line.split(maxsplit=1)[1], 0)
                self.memory[next_data_addr] = wrap32(value)
                next_data_addr += 4
                current_data_label = None
                continue

            if section != "text":
                raise RuntimeError(f"instruction outside text section: {line}")

            parts = line.split(None, 1)
            op = parts[0]
            args = []
            if len(parts) > 1:
                args = [arg.strip() for arg in parts[1].split(",")]
            self.instructions.append(Instruction(op=op, args=args, raw=line))


class Machine:
    MEM_OP_RE = re.compile(r"(-?\d+)\(([^)]+)\)")

    def __init__(self, program: Program) -> None:
        self.program = program
        self.regs: dict[str, int] = {f"x{i}": 0 for i in range(32)}
        self.regs["x2"] = STACK_BASE

    def reg(self, name: str) -> int:
        return self.regs[canon_reg(name)]

    def set_reg(self, name: str, value: int) -> None:
        reg_name = canon_reg(name)
        if reg_name == "x0":
          return
        self.regs[reg_name] = wrap32(value)

    def load_word(self, address: int) -> int:
        if address % 4 != 0:
            raise RuntimeError(f"unaligned load at {address:#x}")
        return self.program.memory.get(address, 0)

    def store_word(self, address: int, value: int) -> None:
        if address % 4 != 0:
            raise RuntimeError(f"unaligned store at {address:#x}")
        self.program.memory[address] = wrap32(value)

    def parse_mem(self, operand: str) -> tuple[int, str]:
        match = self.MEM_OP_RE.fullmatch(operand.replace(" ", ""))
        if match is None:
            raise RuntimeError(f"unsupported memory operand: {operand}")
        return int(match.group(1), 0), canon_reg(match.group(2))

    def label_addr(self, label: str) -> int:
        if label in self.program.data_labels:
            return self.program.data_labels[label]
        raise RuntimeError(f"unknown address label: {label}")

    def run(self) -> int:
        if "main" not in self.program.text_labels:
            raise RuntimeError("program does not define main")

        pc = self.program.text_labels["main"]
        self.set_reg("ra", -1)
        steps = 0

        while True:
            if steps >= STEP_LIMIT:
                raise RuntimeError("step limit exceeded")
            steps += 1

            if pc < 0 or pc >= len(self.program.instructions):
                raise RuntimeError(f"pc out of range: {pc}")

            instruction = self.program.instructions[pc]
            next_pc = pc + 1
            op = instruction.op
            args = instruction.args

            if op == "addi":
                rd, rs, imm = args
                self.set_reg(rd, signed32(self.reg(rs)) + int(imm, 0))
            elif op == "add":
                rd, rs1, rs2 = args
                self.set_reg(rd, signed32(self.reg(rs1)) + signed32(self.reg(rs2)))
            elif op == "sub":
                rd, rs1, rs2 = args
                self.set_reg(rd, signed32(self.reg(rs1)) - signed32(self.reg(rs2)))
            elif op == "mul":
                rd, rs1, rs2 = args
                self.set_reg(rd, signed32(self.reg(rs1)) * signed32(self.reg(rs2)))
            elif op == "div":
                rd, rs1, rs2 = args
                self.set_reg(
                    rd,
                    trunc_div(signed32(self.reg(rs1)), signed32(self.reg(rs2))),
                )
            elif op == "rem":
                rd, rs1, rs2 = args
                lhs = signed32(self.reg(rs1))
                rhs = signed32(self.reg(rs2))
                quotient = trunc_div(lhs, rhs)
                self.set_reg(rd, lhs - quotient * rhs)
            elif op == "slt":
                rd, rs1, rs2 = args
                self.set_reg(
                    rd,
                    1 if signed32(self.reg(rs1)) < signed32(self.reg(rs2)) else 0,
                )
            elif op == "xori":
                rd, rs, imm = args
                self.set_reg(rd, self.reg(rs) ^ int(imm, 0))
            elif op == "neg":
                rd, rs = args
                self.set_reg(rd, -signed32(self.reg(rs)))
            elif op == "seqz":
                rd, rs = args
                self.set_reg(rd, 1 if self.reg(rs) == 0 else 0)
            elif op == "snez":
                rd, rs = args
                self.set_reg(rd, 1 if self.reg(rs) != 0 else 0)
            elif op == "li":
                rd, imm = args
                self.set_reg(rd, int(imm, 0))
            elif op == "la":
                rd, label = args
                self.set_reg(rd, self.label_addr(label))
            elif op == "lw":
                rd, mem = args
                offset, base = self.parse_mem(mem)
                self.set_reg(rd, self.load_word(signed32(self.reg(base)) + offset))
            elif op == "sw":
                rs, mem = args
                offset, base = self.parse_mem(mem)
                self.store_word(signed32(self.reg(base)) + offset, self.reg(rs))
            elif op == "bne":
                rs1, rs2, label = args
                if self.reg(rs1) != self.reg(rs2):
                    next_pc = self.program.text_labels[label]
            elif op == "j":
                (label,) = args
                next_pc = self.program.text_labels[label]
            elif op == "call":
                (label,) = args
                self.set_reg("ra", next_pc)
                next_pc = self.program.text_labels[label]
            elif op == "ret":
                return_pc = signed32(self.reg("ra"))
                if return_pc == -1:
                    return signed32(self.reg("a0"))
                next_pc = return_pc
            else:
                raise RuntimeError(f"unsupported instruction: {instruction.raw}")

            pc = next_pc


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: riscv_runner.py <assembly-file>", file=sys.stderr)
        return 1

    program = Program(Path(sys.argv[1]))
    machine = Machine(program)
    print(machine.run())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
