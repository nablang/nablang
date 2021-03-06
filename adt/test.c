#include <ccut.h>
#include "val.h"
#include "utils/mut-array.h"
#include "utils/mut-map.h"
#include "utils/utf-8.h"

void gens_suite();
void val_suite();
void box_suite();
void map_cola_suite();
void map_node_suite();
void map_suite();
void array_suite();
void dict_suite();
void sym_table_suite();
void string_suite();
void cons_suite();
void struct_suite();

#pragma mark ### tests for basic assumptions

// much code is critical on the word width of the system
// todo move these code into config step?
void base_suite() {
  ccut_test("data size") {
    assert_eq(8, sizeof(void*));
    assert_eq(8, sizeof(Val));
    assert_eq(8, sizeof(uint64_t));
    assert_eq(8, sizeof(unsigned long));
    assert_eq(8, sizeof(unsigned long long));
    assert_eq(8, sizeof(size_t));
    assert_eq(1, sizeof(char));
    assert_eq(-120, (char)(unsigned char)(char)(-120));
    assert_eq(255, (unsigned char)(char)(unsigned char)(255));
  }
}

#pragma mark ### test utils/mut-array.h
typedef struct {
  int x;
  int y;
} MyPoint;

MUT_ARRAY_DECL(Points, MyPoint);

void mut_array_suite() {
  ccut_test("insert elements") {
    struct Points da;
    Points.init(&da, 0);
    for (int i = 0; i < 50; i++) {
      MyPoint p = {i, i*2};
      Points.push(&da, p);
    }
    assert_eq(50, Points.size(&da));

    int mismatch = -1;
    for (int i = 0; i < 50; i++) {
      MyPoint* p = Points.at(&da, i);
      if (i != p->x || i*2 != p->y) {
        mismatch = i;
        break;
      }
    }
    assert_eq(-1, mismatch);
    Points.cleanup(&da);
  }

  ccut_test("remove") {
    struct Points da;
    Points.init(&da, 0);
    for (int i = 0; i < 5; i++) {
      MyPoint p = {i, i*2};
      Points.push(&da, p);
    }
    Points.remove(&da, 4);
    Points.remove(&da, 0);
    assert_eq(3, Points.size(&da));
    assert_eq(1, Points.at(&da, 0)->x);
    assert_eq(2, Points.at(&da, 1)->x);
    assert_eq(3, Points.at(&da, 2)->x);
  }

  ccut_test("reverse") {
    struct Points a;
    Points.init(&a, 3);
    for (int i = 0; i < 3; i++) {
      MyPoint p = {0, i + 1};
      Points.push(&a, p);
    }
    Points.reverse(&a);
    assert_eq(3, Points.at(&a, 0)[0].y);
    assert_eq(2, Points.at(&a, 1)[0].y);
    assert_eq(1, Points.at(&a, 2)[0].y);
  }
}

#pragma mark ### test utils/mut-map.h

static bool mm_eq(uint64_t k1, uint64_t k2) {
  return k1 == k2;
}
static uint64_t mm_hash(uint64_t k) {
  return (k >> 2);
}
MUT_MAP_DECL(MM, uint64_t, uint64_t, mm_hash, mm_eq);

void mut_map_suite() {
  ccut_test("insert different hash") {
    struct MM mm;
    MM.init(&mm);
    MM.insert(&mm, 1 << 2, 1);
    MM.insert(&mm, 2 << 2, 2);

    assert_eq(2, MM.size(&mm));
    uint64_t v;
    assert_true(MM.find(&mm, 1 << 2, &v), "should find key1");
    assert_eq(1, v);
    assert_true(MM.find(&mm, 2 << 2, &v), "should find key2");
    assert_eq(2, v);

    MM.remove(&mm, 1 << 2);
    assert_eq(1, MM.size(&mm));
    assert_false(MM.find(&mm, 1 << 2, &v), "should not find removed key1");

    MM.remove(&mm, 1 << 2);
    assert_eq(1, MM.size(&mm));

    MM.cleanup(&mm);
  }

  ccut_test("insert same key") {
    struct MM mm;
    MM.init(&mm);
    MM.insert(&mm, 1 << 2, 1);
    MM.insert(&mm, 1 << 2, 2);

    assert_eq(1, MM.size(&mm));
    uint64_t v;
    assert_true(MM.find(&mm, 1 << 2, &v), "should find key1");
    assert_eq(2, v);

    MM.cleanup(&mm);
  }

  ccut_test("insert hash collision keys") {
    struct MM mm;
    MM.init(&mm);
    MM.insert(&mm, 0, 0);
    MM.insert(&mm, 1, 1);
    MM.insert(&mm, 2, 2);

    assert_eq(3, MM.size(&mm));
    uint64_t v;
    assert_true(MM.find(&mm, 0, &v), "should find key0");
    assert_eq(0, v);
    assert_true(MM.find(&mm, 1, &v), "should find key1");
    assert_eq(1, v);
    assert_true(MM.find(&mm, 2, &v), "should find key2");
    assert_eq(2, v);

    MM.remove(&mm, 2);
    assert_eq(2, MM.size(&mm));
    assert_true(MM.find(&mm, 0, &v), "should find key0");
    assert_true(MM.find(&mm, 1, &v), "should find key1");
    assert_false(MM.find(&mm, 2, &v), "should not find key2");

    MM.remove(&mm, 0);
    assert_eq(1, MM.size(&mm));
    assert_false(MM.find(&mm, 0, &v), "should not find key0");
    assert_true(MM.find(&mm, 1, &v), "should find key1");
    assert_false(MM.find(&mm, 2, &v), "should not find key2");

    MM.remove(&mm, 1);
    assert_eq(0, MM.size(&mm));
    assert_false(MM.find(&mm, 0, &v), "should not find key0");
    assert_false(MM.find(&mm, 1, &v), "should not find key1");
    assert_false(MM.find(&mm, 2, &v), "should not find key2");

    MM.cleanup(&mm);
  }

  ccut_test("rehash") {
    struct MM mm;
    MM.init(&mm);
    // insert over 64 keys and it should be fine
    for (int i = 0; i < 200; i++) {
      MM.insert(&mm, i, i);
    }
    for (int i = 0; i < 200; i++) {
      uint64_t v;
      if (!MM.find(&mm, i, &v)) {
        assert_true(false, "should find i=%d", i);
      }
    }
    MM.cleanup(&mm);
  }
}

#pragma mark ### test utils/pool.h

typedef struct {
  char data[100];
} Slot;
#define POOL_SLOT_TYPE Slot
#include "utils/pool.h"
void pool_suite() {
  ccut_test("test pool") {
    Pool* p = pool_new();
    for (int i = 0; i < 1000; i++) {
      Slot* slot = pool_slot_alloc(p);
    }
    pool_delete(p);
  }
}

#pragma mark ### test utils/utf-8.h

void utf_8_suite() {
  ccut_test("test utf_8_scan") {
    const char* s = "𝄞";
    int size = strlen(s);
    int c = utf_8_scan(s, &size);
    assert_eq(0x1D11E, c);
    assert_eq(strlen(s), size);

    // truncate
    size = strlen(s) - 1;
    c = utf_8_scan(s, &size);
    assert_eq(-1, c);

    // invalid byte sequence
    s = (char*)((unsigned char[]){0b11011111, 0b11000000}); // 110.* should be followed by 10.*
    size = 2;
    c = utf_8_scan(s, &size);
    assert_eq(-2, c);
  }

  ccut_test("test utf_8_scan_back") {
    const char* s = "𝄞";
    int size = strlen(s);
    int c = utf_8_scan_back(s + size, &size);
    assert_eq(0x1D11E, c);
    assert_eq(strlen(s), size);

    // truncate
    size = strlen(s) - 1;
    c = utf_8_scan_back(s + size, &size);
    assert_eq(-1, c);

    // invalid byte sequence
    s = (char*)((unsigned char[]){0b11011111, 0b11000000}); // 110.* should be followed by 10.*
    size = 2;
    c = utf_8_scan_back(s + size, &size);
    assert_eq(-2, c);
  }

  ccut_test("test utf_8_append") {
    int b1 = utf_8_calc(31526);
    int b2 = utf_8_calc(21495);
    char buf[b1 + b2 + 1];
    buf[b1 + b2] = '\0';

    int a1 = utf_8_append(buf, 0, 31526);
    assert_eq(b1, a1);
    int a2 = utf_8_append(buf, a1, 21495);
    assert_eq(b2, a2);

    assert_eq(0, strcmp(buf, "符号"));
  }
}

#pragma mark ### test utils/arena.h

#include "utils/arena.h"
void arena_suite() {
  ccut_test("test arena") {
    Arena* a = arena_new();
    for (int i = 0; i < 100; i++) {
      void* pointer = arena_slot_alloc(a, i);
    }
    arena_delete(a);
  }

  ccut_test("arena pointer distance") {
    Arena* a = arena_new();
    void* p1 = arena_slot_alloc(a, 2);
    void* p2 = arena_slot_alloc(a, 1);
    assert_eq(2 * sizeof(void*), (uintptr_t)p2 - (uintptr_t)p1);
    arena_delete(a);
  }
}

#pragma mark ### test utils/dual_stack.h

#include "utils/dual-stack.h"
DUAL_STACK_DECL(DStack, int16_t, int32_t);

typedef struct {
  int8_t magic;
  int lbp;
  int rbp;
} DStackFrame;

void dual_stack_suite() {
  ccut_test("test dual stack push/pop") {
    struct DStack d;
    DStack.init(&d);
    for (int i = 0; i < 5; i++) {
      DStack.lpush(&d, i);
      DStack.rpush(&d, i * 9);
      DStack.rpush(&d, i * 10);
    }
    assert_eq(5, DStack.lsize(&d));
    assert_eq(10, DStack.rsize(&d));
    // l:[4,3,2,1,0] r:[40,36, 30,27, 20,18, 10,9, 0,0]

    DStack.lpop(&d);
    for (int i = 0; i < 6; i++) {
      DStack.rpop(&d);
    }
    assert_eq(4, DStack.lsize(&d));
    assert_eq(4, DStack.rsize(&d));
    // l:[3,2,1,0] r:[10,9,0,0]

    int lsum = 0;
    for (int i = 0; i < DStack.lsize(&d); i++) {
      int16_t* l = DStack.lat(&d, i);
      lsum += *l;
    }
    assert_eq(6, lsum);

    int rsum = 0;
    for (int i = 0; i < DStack.rsize(&d); i++) {
      int32_t* r = DStack.rat(&d, i);
      rsum += *r;
    }
    assert_eq(19, rsum);

    DStack.cleanup(&d);
  }

  ccut_test("test dual stack frame management") {
    struct DStack d;
    DStackFrame* dsf;

    DStack.init(&d);

    DStack.lpush(&d, 23);
    int lbp = DStack.lsize(&d);
    int rbp = DStack.rsize(&d);
    dsf = DStack.push_frame(&d, sizeof(DStackFrame));
    dsf->lbp = lbp;
    dsf->rbp = rbp;
    DStack.lpush(&d, 1);
    DStack.rpush(&d, 3);

    DStack.restore(&d, dsf->lbp, dsf->rbp);
    int16_t* ltop = DStack.lat(&d, 0);
    assert_eq(23, *ltop);

    DStack.cleanup(&d);
  }
}

#pragma mark ### run them all

int main (int argc, char const *argv[]) {
  val_trap_backtrace(argv[0]);
  ccut_run_suite(base_suite);
  ccut_run_suite(gens_suite);
  ccut_run_suite(box_suite);
  ccut_run_suite(mut_array_suite);
  ccut_run_suite(mut_map_suite);
  ccut_run_suite(pool_suite);
  ccut_run_suite(utf_8_suite);
  ccut_run_suite(arena_suite);
  ccut_run_suite(dual_stack_suite);
  ccut_run_suite(val_suite);
  ccut_run_suite(array_suite);
  ccut_run_suite(dict_suite);
  ccut_run_suite(map_cola_suite);
  ccut_run_suite(map_node_suite);
  ccut_run_suite(map_suite);
  ccut_run_suite(sym_table_suite);
  ccut_run_suite(string_suite);
  ccut_run_suite(cons_suite);
  ccut_run_suite(struct_suite);
  ccut_print_stats();
  return 0;
}
