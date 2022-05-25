#!/usr/bin/env python3
#  FreeRTOS STM32 Reference Integration
#
#  Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy of
#  this software and associated documentation files (the "Software"), to deal in
#  the Software without restriction, including without limitation the rights to
#  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
#  the Software, and to permit persons to whom the Software is furnished to do so,
#  subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in all
#  copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
#  FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
#  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
#  https://www.FreeRTOS.org
#  https://github.com/FreeRTOS
#

import ast
import operator as op
from argparse import ArgumentParser


class MacroParser(object):
    # supported operators for our safe parser
    _operators = {
        # binary arithmetic operators
        ast.Add: op.add,
        ast.Sub: op.sub,
        ast.Mult: op.mul,
        ast.Div: op.floordiv,
        ast.Mod: op.mod,
        # binary bitwise operators
        ast.BitAnd: op.and_,
        ast.BitOr: op.or_,
        ast.BitXor: op.xor,
        # unary bitwise operators
        ast.Invert: op.invert,
        ast.LShift: op.lshift,
        ast.RShift: op.rshift,
    }

    @staticmethod
    def evaluate_macro_recur(node):
        """Recursively parse the tree"""
        if isinstance(node, ast.Num):
            return node.n
        elif isinstance(node, ast.BinOp):
            return MacroParser._operators[type(node.op)](
                MacroParser.evaluate_macro_recur(node.left),
                MacroParser.evaluate_macro_recur(node.right),
            )
        elif isinstance(node, ast.UnaryOp):
            return MacroParser._operators[type(node.op)](
                MacroParser.evaluate_macro_recur(node.operand)
            )
        elif isinstance(node, ast.Tuple) and len(node.elts) == 1:
            return MacroParser.evaluate_macro_recur(node.elts[0])
        else:
            print(type(node))
            raise TypeError

    @staticmethod
    def evaluate_macro(node):
        # Base case, is a number
        name = ""
        if isinstance(node, ast.Assign):
            if len(node.targets) == 1:
                name = node.targets[0].id
                value = MacroParser.evaluate_macro_recur(node.value)
                return (name, value)
            else:
                raise TypeError
        else:
            print(type(node))
            raise TypeError


def cleanup_lines(lines, prefix):
    lines_out = []
    for line in lines:
        if prefix in line:
            # Remove beginning and trailing tabs, spaces, and commas
            line = line.strip("\t, ")
            lines_out.append(line)
    return lines_out


def main():
    argparser = ArgumentParser()
    argparser.add_argument(
        "--prefix", "-p", help="Prefix for each valid macro.", default="RE_"
    )
    argparser.add_argument(
        "output_file", help="Output file to store key=value pairs in."
    )
    argparser.add_argument(
        "input_files", help="Preprocessed input file(s) to parse.", nargs="*"
    )
    args = argparser.parse_args()

    input_files = args.input_files
    output_file = args.output_file
    prefix = args.prefix

    output_dict = dict()

    for file_name in input_files:
        with open(file_name, "r") as f:
            lines = cleanup_lines(f.readlines(), prefix)
            filtered_file = "\n".join(lines)
            parsed_file = ast.parse(filtered_file)
            for statemnt in parsed_file.body:
                rslt = MacroParser.evaluate_macro(statemnt)
                if isinstance(rslt, tuple):
                    (key, value) = rslt
                    output_dict[key] = value

    with open(output_file, "w") as f:
        for k, v in output_dict.items():
            f.write("{}=0x{:X}\n".format(k, v))


if __name__ == "__main__":
    main()
