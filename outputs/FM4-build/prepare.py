"""Generate private switch labels by evaluating bounded dispatches in user data.

Recipes contain addresses/counts only. Instruction words and table values are
read from the locally loaded image, never embedded here or in the recipes.
"""
import json
from pathlib import Path
import struct

BASE = 0x82000000

def switches(image, recipes):
    def read(address, size=4):
        offset = address - BASE
        if not 0 <= offset <= len(image) - size:
            raise ValueError(f'Image read outside bounds: {address:08X}')
        return int.from_bytes(image[offset:offset+size], 'big')

    result = []
    for recipe in recipes:
        labels = []
        for index in range(recipe['count']):
            regs = {recipe['register']: index}
            ctr = None
            for address in range(recipe['start'], recipe['end'] + 1, 4):
                word = read(address)
                op, rt, ra, rb = word >> 26, (word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31
                immediate = (word & 65535) - (65536 if word & 32768 else 0)
                xo = (word >> 1) & 1023
                if word == 0x60000000:  # nop
                    pass
                elif op in (14, 15):  # addi / addis, including li / lis
                    regs[rt] = ((regs[ra] if ra else 0) + (immediate << (16 if op == 15 else 0))) & 0xFFFFFFFF
                elif op == 21:  # rlwinm
                    sh, mb, me = rb, (word >> 6) & 31, (word >> 1) & 31
                    bits = list(range(mb, me+1)) if mb <= me else list(range(mb,32)) + list(range(me+1))
                    mask = sum(1 << (31-i) for i in bits)
                    value = regs[rt]
                    regs[ra] = ((value << sh) | (value >> (32-sh))) & mask
                elif op == 31 and xo == 266:  # add
                    regs[rt] = (regs[ra] + regs[rb]) & 0xFFFFFFFF
                elif op == 31 and xo == 444:  # or, including mr
                    regs[ra] = regs[rt] | regs[rb]
                elif op == 31 and xo in (23, 279, 87):  # lwzx, lhzx, lbzx
                    regs[rt] = read(((regs[ra] if ra else 0) + regs[rb]) & 0xFFFFFFFF, {23:4,279:2,87:1}[xo])
                elif op == 31 and xo == 467 and (ra | rb << 5) == 9:  # mtctr
                    ctr = regs[rt]
                elif word == 0x4E800420:  # bctr ends this straight-line path
                    if ctr is None or ctr % 4 or not BASE <= ctr < BASE + len(image):
                        raise ValueError('Invalid switch destination')
                    labels.append(ctr)
                    break
                else:
                    raise ValueError(f'Unsupported dispatch instruction at {address:08X}')
            if len(labels) != index+1:
                raise ValueError('Dispatch did not terminate at bctr')
        result.append({'address': recipe['address'], 'register': recipe['register'], 'labels':labels})
    return result

def write_switches(image_path, recipe_path, destination):
    tables = switches(Path(image_path).read_bytes(), json.loads(Path(recipe_path).read_text()))
    lines = ['# PRIVATE: generated from the user-provided executable. Do not commit.']
    for table in tables:
        lines += ['\n[[switch_tables]]', f'address = 0x{table["address"]:08X}',
                  f'register = {table["register"]}', 'labels = [' + ', '.join(f'0x{x:08X}' for x in table['labels']) + ']']
    Path(destination).write_text('\n'.join(lines)+'\n')
    return len(tables)
