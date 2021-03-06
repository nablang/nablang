#include "compile.h"
#include <adt/utils/utf-8.h>
#include <adt/utils/str.h>
#include "vm-regexp-op-codes.h"

Val sb_vm_regexp_from_string(struct Iseq* iseq, Val s) {
  if (VAL_KLASS(s) != KLASS_STRING) {
    return nb_string_new_literal_c("not string");
  }

  const char* ptr = nb_string_ptr(s);
  int size = nb_string_byte_size(s);

  int original_iseq_pos = Iseq.size(iseq);
  ENCODE_META(iseq);
  for (int i = 0; i < size; i++) {
    ENCODE(iseq, Arg32, ((Arg32){CHAR, ptr[i]}));
  }
  ENCODE(iseq, uint16_t, MATCH);
  ENCODE(iseq, uint16_t, END);
  ENCODE_FILL_META(iseq, original_iseq_pos, NULL);

  return VAL_NIL;
}

MUT_ARRAY_DECL(Stack, Val);

static Val MATCH_NODE=0, LABEL_NODE=0, JMP_NODE=0, FORK_NODE=0, ATOMIC_NODE=0, SAVE_NODE=0;

static void _ensure_tags() {
  if (!MATCH_NODE) {
    void* ptr  = malloc(sizeof(Val) * 6);
    MATCH_NODE = (Val)ptr;
    LABEL_NODE = (Val)(ptr + 1);
    JMP_NODE   = (Val)(ptr + 2);
    FORK_NODE  = (Val)(ptr + 3);
    ATOMIC_NODE = (Val)(ptr + 4);
    SAVE_NODE = (Val)(ptr + 5);
  }
}

static void _push_label(struct Stack* stack, Val label) {
  Stack.push(stack, label);
  Stack.push(stack, LABEL_NODE);
}

static void _push_atomic(struct Stack* stack, Val label) {
  Stack.push(stack, label);
  Stack.push(stack, ATOMIC_NODE);
}

static void _push_fork(struct Stack* stack, Val label1, Val label2) {
  Stack.push(stack, label2);
  Stack.push(stack, label1);
  Stack.push(stack, FORK_NODE);
}

static void _push_jmp(struct Stack* stack, Val label) {
  Stack.push(stack, label);
  Stack.push(stack, JMP_NODE);
}

static void _push_seq(struct Stack* stack, Val seq) {
  Val content = nb_struct_get(seq, 0);
  for (Val tail = content; tail != VAL_NIL; tail = nb_cons_tail(tail)) {
    Stack.push(stack, nb_cons_head(tail));
  }
}

static void _push_branches(struct Stack* stack, struct Labels* labels, Val branches) {
  /* example encoding e1 | e2 | e3

    fork L1 L2
    L1: e1
    jmp L0
    L2:

    fork L3 L4
    L3: e2
    jmp L0
    L4:

    e3
    L0:

  */

  Val label0 = LABEL_NEW_NUM(labels);
  _push_label(stack, label0);
  Val tail = branches;
  if (tail != VAL_NIL) {
    Stack.push(stack, nb_cons_head(tail));
    tail = nb_cons_tail(tail);
  }

  for (; tail != VAL_NIL; tail = nb_cons_tail(tail)) {

    Val label1 = LABEL_NEW_NUM(labels);
    Val label2 = LABEL_NEW_NUM(labels);

    _push_label(stack, label2);
    _push_jmp(stack, label0);
    Stack.push(stack, nb_cons_head(tail));
    _push_label(stack, label1);

    _push_fork(stack, label1, label2);
  }
}

static void _push_quantified(struct Stack* stack, struct Labels* labels, Val node) {
  Val e = nb_struct_get(node, 0);
  Val quantifier = nb_struct_get(node, 1);

  const char* ptr = nb_string_ptr(quantifier);
  int len = nb_string_byte_size(quantifier);

  if (len == 1) { // greedy
    switch (ptr[0]) {
      case '?': {
        // fork L1 L2
        // L1: e
        // L2:
        Val l1 = LABEL_NEW_NUM(labels);
        Val l2 = LABEL_NEW_NUM(labels);
        _push_label(stack, l2);
        Stack.push(stack, e);
        _push_label(stack, l1);
        _push_fork(stack, l1, l2);
        break;
      }
      case '+': {
        // L1: e
        // fork L1 L2
        // L2
        Val l1 = LABEL_NEW_NUM(labels);
        Val l2 = LABEL_NEW_NUM(labels);
        _push_label(stack, l2);
        _push_fork(stack, l1, l2);
        Stack.push(stack, e);
        _push_label(stack, l1);
        break;
      }
      case '*': {
        // L1: fork L2 L3
        // L2: e
        // jmp L1
        // L3:
        Val l1 = LABEL_NEW_NUM(labels);
        Val l2 = LABEL_NEW_NUM(labels);
        Val l3 = LABEL_NEW_NUM(labels);
        _push_label(stack, l3);
        _push_jmp(stack, l1);
        Stack.push(stack, e);
        _push_label(stack, l2);
        _push_fork(stack, l2, l3);
        _push_label(stack, l1);
        break;
      }
    }
  } else {
    switch (ptr[1]) {
      case '?': { // reluctant
        switch (ptr[0]) {
          case '?': {
            // fork L2 L1
            // L1: e
            // L2:
            Val l1 = LABEL_NEW_NUM(labels);
            Val l2 = LABEL_NEW_NUM(labels);
            _push_label(stack, l2);
            Stack.push(stack, e);
            _push_label(stack, l1);
            _push_fork(stack, l2, l1);
            break;
          }
          case '+': {
            // L1: e
            // fork L2 L1
            // L2:
            Val l1 = LABEL_NEW_NUM(labels);
            Val l2 = LABEL_NEW_NUM(labels);
            _push_label(stack, l2);
            _push_fork(stack, l2, l1);
            Stack.push(stack, e);
            _push_label(stack, l1);
            break;
          }
          case '*': {
            // L1: fork L3 L2
            // L2: e
            // jmp L1
            // L3:
            Val l1 = LABEL_NEW_NUM(labels);
            Val l2 = LABEL_NEW_NUM(labels);
            Val l3 = LABEL_NEW_NUM(labels);
            _push_label(stack, l3);
            _push_jmp(stack, l1);
            Stack.push(stack, e);
            _push_label(stack, l2);
            _push_fork(stack, l3, l2);
            _push_label(stack, l1);
            break;
          }
        }
        break;
      }
      case '+': { // possessive
        Val l = LABEL_NEW_NUM(labels);
        _push_label(stack, l);
        Val args[] = {e, nb_string_new_literal(1, ptr)};
        Val node = nb_struct_new(VAL_KLASS(node), 2, args);
        Stack.push(stack, node);
        _push_atomic(stack, l);
        break;
      }
    }
  }
}

// good: returns 0
// negative range: returns 1
// from > to: returns 2
// range overflow: returns 3
static int _push_quantified_range(struct Stack* stack, struct Labels* labels, Val node) {
  // node[e, from, to?, reluctant/possessive mark]
  Val e = nb_struct_get(node, 0);
  Val v_from = nb_struct_get(node, 1);
  Val v_to_maybe = nb_struct_get(node, 2);
  Val kind = nb_struct_get(node, 3);

  if (kind != VAL_NIL) {
    fatal_err("reluctant/possessive mark not supported yet");
  }

  int64_t from, to;
  from = VAL_TO_INT(v_from);
  if (from < 0) {
    return 1;
  }
  if (from > (1 << 30)) { // 1G, too large
    return 3;
  }
  if (v_to_maybe == VAL_NIL) {
    to = from;
  } else {
    to = VAL_TO_INT(nb_cons_head(to));
    if (from > to) {
      return 2;
    }
  }

  // example: e{2,5} -> e e e? e? e?
  for (int64_t i = 0; i < from; i++) {
    Stack.push(stack, e);
  }
  for (int64_t i = from; i < to; i++) {
    // fork L1 L2
    // L1: e
    // L2:
    Val l1 = LABEL_NEW_NUM(labels);
    Val l2 = LABEL_NEW_NUM(labels);
    _push_label(stack, l2);
    Stack.push(stack, e);
    _push_label(stack, l1);
    _push_fork(stack, l1, l2);
  }
  return 0;
}

static void _push_group(struct Stack* stack, int* max_capture, Val group_node) {
  Val special = nb_struct_get(group_node, 0);
  Val content = nb_struct_get(group_node, 1);

  int special_size = nb_string_byte_size(special);
  const char* special_ptr = nb_string_ptr(special);

# define IF_MATCH(str) if (str_compare(special_size, special_ptr, strlen(str), str) == 0)

  if (special_size == 0) {
    *max_capture += 2;
    Stack.push(stack, *max_capture + 1);
    Stack.push(stack, SAVE_NODE);
    Stack.push(stack, content);
    Stack.push(stack, *max_capture);
    Stack.push(stack, SAVE_NODE);

  } else IF_MATCH("?:") {
    Stack.push(stack, content);

  } else IF_MATCH("?>") {
    // todo
  } else IF_MATCH("?<=") {
    // todo
  } else IF_MATCH("?<!") {
    // todo
  } else IF_MATCH("?=") {
    // todo
  } else IF_MATCH("?!") {
    // todo
  }

# undef IF_MATCH

}

static void _encode_char(struct Iseq* iseq, Val char_node, bool ignore_case) {
  int chr = VAL_TO_INT(char_node);
  // TODO ignore case
  ENCODE(iseq, Arg32, ((Arg32){CHAR, chr}));
}

static void _encode_anchor(struct Iseq* iseq, Val anchor) {
  Val str = nb_struct_get(anchor, 0);
  const char* s = nb_string_ptr(str);
  switch (s[0]) {
    case '^': {
      ENCODE(iseq, uint16_t, ANCHOR_BOL);
      break;
    }
    case '$': {
      ENCODE(iseq, uint16_t, ANCHOR_EOL);
      break;
    }
    case '\\': {
      switch (s[1]) {
        case 'b': {
          ENCODE(iseq, uint16_t, ANCHOR_WBOUND);
          break;
        }
        case 'B': {
          ENCODE(iseq, uint16_t, ANCHOR_N_WBOUND);
          break;
        }
        case 'a': {
          ENCODE(iseq, uint16_t, ANCHOR_BOS);
          break;
        }
        case 'A': {
          ENCODE(iseq, uint16_t, ANCHOR_N_BOS);
          break;
        }
        case 'z': {
          ENCODE(iseq, uint16_t, ANCHOR_EOS);
          break;
        }
        case 'Z': {
          ENCODE(iseq, uint16_t, ANCHOR_N_EOS);
          break;
        }
      }
      break;
    }
  }
}

static void _encode_predef_char_group(struct Iseq* iseq, Val node) {
  const char* ptr = nb_string_ptr(nb_struct_get(node, 0));
  if (ptr[0] == '.') {
    ENCODE(iseq, uint16_t, CG_ANY);
    return;
  }

  switch (ptr[1]) {
    case 'd': {
      ENCODE(iseq, uint16_t, CG_D);
      return;
    }
    case 'D': {
      ENCODE(iseq, uint16_t, CG_N_D);
      return;
    }
    case 'w': {
      ENCODE(iseq, uint16_t, CG_W);
      return;
    }
    case 'W': {
      ENCODE(iseq, uint16_t, CG_N_W);
      return;
    }
    case 'h': {
      ENCODE(iseq, uint16_t, CG_H);
      return;
    }
    case 'H': {
      ENCODE(iseq, uint16_t, CG_N_H);
      return;
    }
    case 's': {
      ENCODE(iseq, uint16_t, CG_S);
      return;
    }
    case 'S': {
      ENCODE(iseq, uint16_t, CG_N_S);
      return;
    }
  }
}

static int _compare_range(const void* rg1, const void* rg2) {
  int64_t f1 = VAL_TO_INT(nb_struct_get(*((Val*)rg1), 0));
  int64_t f2 = VAL_TO_INT(nb_struct_get(*((Val*)rg2), 0));
  return f1 > f2 ? 1 : f1 < f2 ? -1 : 0;
}

static Val _invert_ranges(Val list, uint32_t range_klass) {
  if (list == VAL_NIL) {
    Val argv[] = {VAL_FROM_INT(0), VAL_FROM_INT(UTF_8_MAX)};
    Val e = nb_struct_new(range_klass, 2, argv);
    return nb_cons_new(e, VAL_NIL);
  }

  struct Vals vals;
  Vals.init(&vals, 5);
  for (; list != VAL_NIL; list = nb_cons_tail(list)) {
    Vals.push(&vals, nb_cons_head(list));
  }
  qsort(Vals.at(&vals, 0), Vals.size(&vals), sizeof(Val), _compare_range);

  Val res = VAL_NIL;
  Val first_node = *Vals.at(&vals, 0);
  int32_t last_from = (int32_t)VAL_TO_INT(nb_struct_get(first_node, 0));
  int32_t last_to = (int32_t)VAL_TO_INT(nb_struct_get(first_node, 1));
  Val preserve;
  if (last_from > 0) {
    Val e = nb_struct_new(range_klass, 2, (Val[]){VAL_FROM_INT(0), VAL_FROM_INT(last_from - 1)});
    preserve = e;
    res = nb_cons_new(e, res);
  }

  for (int i = 1; i < Vals.size(&vals); i++) {
    Val* range = Vals.at(&vals, i);
    int32_t from = (int32_t)VAL_TO_INT(nb_struct_get(*range, 0));
    int32_t to = (int32_t)VAL_TO_INT(nb_struct_get(*range, 1));
    // printf("\n\n%d,%d,%d,%d\n\n", from, to, last_from, last_to);
    if (from > last_to + 1) { // disjoint
      Val e = nb_struct_new(range_klass, 2, (Val[]){VAL_FROM_INT(last_to + 1), VAL_FROM_INT(from - 1)});
      res = nb_cons_new(e, res);
      last_from = from;
      last_to = to;
    } else if (to > last_to) { // overlapped
      last_to = to;
    } else {
      // included in last chunk
    }
  }
  Vals.cleanup(&vals);

  if (last_to + 1 <= UTF_8_MAX) {
    Val e = nb_struct_new(range_klass, 2, (Val[]){VAL_FROM_INT(last_to + 1), VAL_FROM_INT(UTF_8_MAX)});
    res = nb_cons_new(e, res);
  }

  return res;
}

// return list of char ranges
// todo language dependent ignore case
static Val _flatten_char_group(Val node, bool ignore_case, Val result_list) {
  // BracketCharGroup[beginer, (CharRange | BracketCharGroup)+]
  uint32_t kCharGroup = VAL_KLASS(node);
  uint32_t kCharRange = klass_find_c("CharRange", sb_klass());

  // begin.char-group attached boolean
  bool is_positive = (nb_struct_get(node, 0) == VAL_TRUE);

  Val list = nb_struct_get(node, 1);
  for (; list != VAL_NIL; list = nb_cons_tail(list)) {
    Val e = nb_cons_head(list);
    // NOTE if there is overlap, let it be
    if (VAL_KLASS(e) == kCharRange) {
      result_list = nb_cons_new(e, result_list);
    } else {
      Val invert_list = _flatten_char_group(e, ignore_case, VAL_NIL);
      for (; invert_list != VAL_NIL; invert_list = nb_cons_tail(invert_list)) {
        result_list = nb_cons_new(nb_cons_head(invert_list), result_list);
      }
    }
  }

  if (is_positive) {
    return result_list;
  } else {
    return _invert_ranges(result_list, kCharRange);
  }
}

static void _encode_char_group(struct Iseq* iseq, struct Labels* labels, Val ranges) {
  // example encoding of [a-b c-d e-f]:
  //   jif_range a b L0
  //   jif_range c d L0
  //   jif_range e f L0
  //   die
  //   L0:

  int32_t l0 = LABEL_NEW_NUM(labels);
  for (Val tail = ranges; tail != VAL_NIL; tail = nb_cons_tail(tail)) {
    Val head = nb_cons_head(tail);
    int32_t from = (int32_t)VAL_TO_INT(nb_struct_get(head, 0));
    int32_t to = (int32_t)VAL_TO_INT(nb_struct_get(head, 1));
    Arg323232 payload = {JIF_RANGE, from, to, l0};
    ENCODE(iseq, Arg323232, payload);
    LABEL_REF(labels, Iseq.size(iseq) - 2); // last 2-bytes
  }

  // die
  ENCODE(iseq, uint16_t, DIE);

  // L0:
  int offset = Iseq.size(iseq);
  LABEL_DEF(labels, l0, offset);
}

Val sb_vm_regexp_compile(struct Iseq* iseq, Val patterns_dict, Val node) {
  _ensure_tags();

  uint32_t kSeq               = klass_find_c("Seq", sb_klass());
  uint32_t kPredefAnchor      = klass_find_c("PredefAnchor", sb_klass());
  uint32_t kFlag              = klass_find_c("Flag", sb_klass());
  uint32_t kQuantified        = klass_find_c("Quantified", sb_klass());
  uint32_t kQuantifiedRange   = klass_find_c("QuantifiedRange", sb_klass());
  uint32_t kGroup             = klass_find_c("Group", sb_klass());
  uint32_t kCharGroupPredef   = klass_find_c("CharGroupPredef", sb_klass());
  uint32_t kUnicodeCharClass  = klass_find_c("UnicodeCharClass", sb_klass());
  uint32_t kPredefInterpolate = klass_find_c("PredefInterpolate", sb_klass());
  uint32_t kBracketCharGroup  = klass_find_c("BracketCharGroup", sb_klass());

  bool ignore_case = false;
  int max_capture = 0;
  struct Stack stack;
  struct Labels labels;
  Stack.init(&stack, 25);
  Labels.init(&labels);

  // encode meta data
  int iseq_original_size = Iseq.size(iseq);
  ENCODE_META(iseq);

  /*
  A compile stack is used to eliminate recursion and reduce physical stack usage.
  If a node requires encoding "summarizing" part after encoding children nodes,
  a special sequence is pushed to compile stack.

  compile stack layout
  - nodes to process are pushed from back to forth.
    since list nodes are already constructed in reverse order,
    so no need to reverse before pushing.
  - special nodes are pushed by compiler, and contents are inlined:
    - match:  [MATCH_NODE]
    - label:  [label_num, LABEL_NODE]
    - jmp:    [label_num, JMP_NODE]
    - fork:   [label_num2, label_num1, FORK_NODE]
    - atomic: [label_num, LABEL_NODE] e [label_num, ATOMIC_NODE]
    - save:   [n, SAVE_NODE]

  encoding of fork, jmp & labels
  1. if meet a label node: fill labels.size as label_num, increase lables.size
  2. if meet a node that refs label: encode label_num instead
  3. go through Splits and Jmps, replace all label numbers with label pos
  */

  Stack.push(&stack, MATCH_NODE);

  // loop compile
  Val content = nb_struct_get(node, 0);
  Stack.push(&stack, content);
  while (Stack.size(&stack)) {
    Val curr = Stack.pop(&stack);

    if (curr == LABEL_NODE) {
      Val label_num = Stack.pop(&stack);
      int offset = Iseq.size(iseq);
      LABEL_DEF(&labels, label_num, offset);
      continue;

    } else if (curr == FORK_NODE) {
      int32_t offset1 = (int32_t)Stack.pop(&stack);
      int32_t offset2 = (int32_t)Stack.pop(&stack);
      LABEL_REF(&labels, Iseq.size(iseq) + 1); // offset1
      LABEL_REF(&labels, Iseq.size(iseq) + 3); // offset2
      ENCODE(iseq, Arg3232, ((Arg3232){FORK, offset1, offset2}));
      continue;

    } else if (curr == JMP_NODE) {
      int32_t offset = Stack.pop(&stack);
      LABEL_REF(&labels, Iseq.size(iseq) + 1);
      ENCODE(iseq, Arg32, ((Arg32){JMP, offset}));
      continue;

    } else if (curr == MATCH_NODE) {
      ENCODE(iseq, uint16_t, MATCH);
      continue;

    } else if (curr == ATOMIC_NODE) {
      int32_t offset = (int32_t)Stack.pop(&stack);
      LABEL_REF(&labels, Iseq.size(iseq) + 1);
      ENCODE(iseq, Arg32, ((Arg32){ATOMIC, offset}));
      continue;

    } else if (curr == SAVE_NODE) {
      uint16_t slot = (uint16_t)Stack.pop(&stack);
      ENCODE(iseq, Arg16, ((Arg16){SAVE, slot}));
      continue;
    }

    uint32_t klass = VAL_KLASS(curr);
    if (klass == KLASS_INTEGER) { // char
      _encode_char(iseq, curr, ignore_case);

    } else if (klass == kSeq) {
      _push_seq(&stack, curr);

    } else if (klass == KLASS_CONS) { // branches
      _push_branches(&stack, &labels, curr);

    } else if (klass == kPredefAnchor) {
      _encode_anchor(iseq, curr);

    } else if (klass == kQuantified) {
      _push_quantified(&stack, &labels, curr);

    } else if (klass == kQuantifiedRange) {
      int res = _push_quantified_range(&stack, &labels, curr);
      if (res != 0) {
        Stack.cleanup(&stack);
        Labels.cleanup(&labels);
        return nb_string_new_literal_c("bad char range");
      }

    } else if (klass == kGroup) {
      _push_group(&stack, &max_capture, curr);

    } else if (klass == kCharGroupPredef) {
      _encode_predef_char_group(iseq, curr);

    } else if (klass == kUnicodeCharClass) {
      // TODO

    } else if (klass == kPredefInterpolate) {
      // TODO

    } else if (klass == kBracketCharGroup) {
      Val ranges = _flatten_char_group(curr, ignore_case, VAL_NIL);
      _encode_char_group(iseq, &labels, ranges);

    } else if (klass == kFlag) {
      Val flag = nb_struct_get(curr, 0);
      if (flag == VAL_TRUE) {
        ignore_case = true; // todo language-dependent ignore case
      } else if (flag == VAL_FALSE) {
        ignore_case = false;
      } else {
        fatal_err("encoding flag not supported yet");
      }

    } else {
      Stack.cleanup(&stack);
      Labels.cleanup(&labels);
      return nb_string_new_f("unrecognized AST node klass %u", klass);
    }
  }

  LABEL_TRANSLATE(&labels, iseq);

  Stack.cleanup(&stack);
  Labels.cleanup(&labels);

  ENCODE(iseq, uint16_t, END);

  ENCODE_FILL_META(iseq, iseq_original_size, NULL);
  return VAL_NIL;
}

void sb_vm_regexp_decompile(uint16_t* pc_start) {
  uint16_t* pc = pc_start;
  uint32_t size = DECODE(ArgU32, pc).arg1;
  uint16_t* pc_end = pc_start + size;
  DECODE(void*, pc);

  while (pc < pc_end) {
    printf("%ld: %s", pc - pc_start, op_code_names[*pc]);
    switch (*pc) {

      case ANCHOR_BOL:
      case ANCHOR_EOL:
      case ANCHOR_WBOUND:
      case ANCHOR_N_WBOUND:
      case ANCHOR_BOS:
      case ANCHOR_N_BOS:
      case ANCHOR_EOS:
      case ANCHOR_N_EOS:
      case CG_ANY:
      case CG_D:
      case CG_N_D:
      case CG_W:
      case CG_N_W:
      case CG_H:
      case CG_N_H:
      case CG_S:
      case CG_N_S:
      case MATCH:
      case DIE: {
        printf("\n");
        pc++;
        break;
      }

      case END: {
        printf("\n");
        pc++;
        if (pc != pc_end) {
          fatal_err("end ins %d and pc_end %d not match", (int)pc, (int)pc_end);
        }
        break;
      }

      case SAVE: {
        printf(" %d\n", DECODE(Arg16, pc).arg1);
        break;
      }

      case JMP:
      case CHAR: {
        printf(" %d\n", DECODE(Arg32, pc).arg1);
        break;
      }

      case FORK:
      case ATOMIC:
      case AHEAD:
      case N_AHEAD: {
        Arg3232 offsets = DECODE(Arg3232, pc);
        printf(" %d %d\n", offsets.arg1, offsets.arg2);
        break;
      }

      case JIF_RANGE: {
        Arg323232 payload = DECODE(Arg323232, pc);
        printf(" %d %d %d\n", payload.arg1, payload.arg2, payload.arg3);
        break;
      }

      case SET: {
        int32_t size = DECODE(Arg32, pc).arg1;
        printf(" %d [", size);
        for (int32_t i = 0; i < size - 1; i++) {
          printf("%d ", DECODE(int32_t, pc));
        }
        if (size > 0) {
          printf("%d", DECODE(int32_t, pc));
        }
        printf("]\n");
      }

      default: {
        fatal_err("bad pc: %d", (int)*pc);
      }
    }
  }
}
