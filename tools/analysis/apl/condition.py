"""Parse and evaluate SimulationCraft action-list conditions.

A condition is only useful whole. Pulling one clause out of a conjunction
measures a rule that was never in force: `annihilation` is gated on
`buff.rending_strike.up & buff.glaive_flurry.down & debuff.reavers_mark.stack<2`,
and testing the middle clause alone reported a 23% violation rate for a line
that never fires, because Rending Strike is never up when Annihilation is cast.

So this parses the whole expression and evaluates every term. A term the log
cannot answer makes the result UNKNOWN, which propagates: a caller that gets
UNKNOWN must decline to report rather than guess. Three-valued logic keeps the
useful cases - `false & unknown` is still false, and `true | unknown` is still
true - so a condition is often decidable even when part of it is not.
"""

from __future__ import annotations
import re
from dataclasses import dataclass
from typing import Callable, Optional

UNKNOWN = None          # third truth value; None so `is None` reads naturally


# --- lexer ------------------------------------------------------------------

TOKEN = re.compile(r"""
    (?P<num>\d+\.?\d*)
  | (?P<name>[a-z_][a-z_0-9]*(?:\.[a-z_0-9]+)*)
  | (?P<op><=|>=|!=|>\?|<\?|[&|!()<>=*/+-])
""", re.VERBOSE)


def tokenize(text: str) -> list:
    out, pos = [], 0
    text = text.replace(' ', '')
    while pos < len(text):
        m = TOKEN.match(text, pos)
        if not m:
            raise ValueError('cannot lex at %d: %r' % (pos, text[pos:pos + 20]))
        out.append(m.group(0))
        pos = m.end()
    return out


# --- parser -----------------------------------------------------------------
#
# Precedence, loosest first, following SimC:
#   |   or
#   &   and
#   comparison  < <= > >= = !=
#   + -
#   * /  >? <?      (>? and <? are SimC's max and min)
#   ! unary, parentheses

@dataclass
class Node:
    kind: str                 # 'or' 'and' 'not' 'cmp' 'arith' 'num' 'term'
    op: str = ''
    left: object = None
    right: object = None
    value: float = 0.0
    name: str = ''


class Parser:
    def __init__(self, tokens):
        self.t, self.i = tokens, 0

    def peek(self):
        return self.t[self.i] if self.i < len(self.t) else None

    def take(self, expected=None):
        tok = self.t[self.i]
        if expected and tok != expected:
            raise ValueError('expected %r, found %r' % (expected, tok))
        self.i += 1
        return tok

    def parse(self):
        node = self.or_()
        if self.i != len(self.t):
            raise ValueError('trailing tokens: %s' % (self.t[self.i:],))
        return node

    def or_(self):
        node = self.and_()
        while self.peek() == '|':
            self.take()
            node = Node('or', left=node, right=self.and_())
        return node

    def and_(self):
        node = self.cmp_()
        while self.peek() == '&':
            self.take()
            node = Node('and', left=node, right=self.cmp_())
        return node

    def cmp_(self):
        node = self.add_()
        while self.peek() in ('<', '<=', '>', '>=', '=', '!='):
            op = self.take()
            node = Node('cmp', op=op, left=node, right=self.add_())
        return node

    def add_(self):
        node = self.mul_()
        while self.peek() in ('+', '-'):
            op = self.take()
            node = Node('arith', op=op, left=node, right=self.mul_())
        return node

    def mul_(self):
        node = self.unary()
        while self.peek() in ('*', '/', '>?', '<?'):
            op = self.take()
            node = Node('arith', op=op, left=node, right=self.unary())
        return node

    def unary(self):
        if self.peek() == '!':
            self.take()
            return Node('not', left=self.unary())
        if self.peek() == '(':
            self.take('(')
            node = self.or_()
            self.take(')')
            return node
        tok = self.take()
        if re.fullmatch(r'\d+\.?\d*', tok):
            return Node('num', value=float(tok))
        return Node('term', name=tok)


def parse(text):
    return Parser(tokenize(text)).parse()


# --- evaluation -------------------------------------------------------------

def truth(value):
    """A bare number is true when non-zero, as SimC treats it."""
    if value is UNKNOWN:
        return UNKNOWN
    if isinstance(value, bool):
        return value
    return float(value) != 0.0


def evaluate(node, resolve):
    """Evaluate a parsed condition.

    `resolve(term)` returns a number, a bool, or UNKNOWN. Returning UNKNOWN is
    the honest answer for anything the tables do not carry, and it is what stops
    a guard being reported on evidence that does not exist.
    """
    if node.kind == 'num':
        return node.value

    if node.kind == 'term':
        return resolve(node.name)

    if node.kind == 'not':
        inner = truth(evaluate(node.left, resolve))
        return UNKNOWN if inner is UNKNOWN else (not inner)

    if node.kind == 'and':
        left = truth(evaluate(node.left, resolve))
        if left is False:
            return False                     # short circuit survives unknowns
        right = truth(evaluate(node.right, resolve))
        if right is False:
            return False
        return UNKNOWN if (left is UNKNOWN or right is UNKNOWN) else True

    if node.kind == 'or':
        left = truth(evaluate(node.left, resolve))
        if left is True:
            return True
        right = truth(evaluate(node.right, resolve))
        if right is True:
            return True
        return UNKNOWN if (left is UNKNOWN or right is UNKNOWN) else False

    left, right = evaluate(node.left, resolve), evaluate(node.right, resolve)
    if left is UNKNOWN or right is UNKNOWN:
        return UNKNOWN

    if node.kind == 'arith':
        left, right = float(left), float(right)
        return {'+': left + right, '-': left - right,
                '*': left * right, '/': left / right if right else 0.0,
                '>?': max(left, right), '<?': min(left, right)}[node.op]

    left, right = float(left), float(right)
    return {'<': left < right, '<=': left <= right, '>': left > right,
            '>=': left >= right, '=': left == right,
            '!=': left != right}[node.op]


def terms(node):
    """Every term in the tree, for reporting which ones went unresolved."""
    if node.kind == 'term':
        return {node.name}
    found = set()
    for child in (node.left, node.right):
        if child is not None:
            found |= terms(child)
    return found
