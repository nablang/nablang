#pragma once

// functions that are only used in compile

#include "spellbreak.h"
#include <adt/dict.h>

void nb_spellbreak_inline_partial_references(PdlexNodeArena* arena, Val main_node);

void nb_spellbreak_build_patterns_map(PdlexNodeArena* arena, Val main_node, Spellbreak* spellbreak);

struct VmLexStruct;
struct VmPegStruct;
struct VmCallbackStruct;
struct VmRegexpStruct;

typedef struct VmLexStruct VmLex;
typedef struct VmPegStruct VmPeg;
typedef struct VmCallbackStruct VmCallback;
typedef struct VmRegexpStruct VmRegexp;

VmLex* nb_vm_lex_compile(PdlexNodeArena* arena, Val node, Spellbreak* spellbreak);
VmPeg* nb_vm_peg_compile(PdlexNodeArena* arena, Val node, Spellbreak* spellbreak);
VmCallback* nb_vm_callback_compile(PdlexNodeArena* arena, Val node, Spellbreak* spellbreak, Val lex_name);
VmRegexp* nb_vm_regexp_compile(PdlexNodeArena* arena, Val node, Spellbreak* spellbreak);
VmRegexp* nb_vm_regexp_from_string(Val node);

int64_t nb_vm_lex_exec(VmLex* lex, Ctx* ctx);
int64_t nb_vm_peg_exec(VmPeg* peg, Ctx* ctx);
int64_t nb_vm_callback_exec(VmCallback* callback, Ctx* ctx);
int64_t nb_vm_regexp_exec(VmRegexp* regexp, Ctx* ctx);

#pragma mark ## some helper macros for compiling

#define LOC(tok) (((TokenNode*)(tok))->loc)

#define S nb_string_new_literal_c

#define AT(node, i) (((SyntaxNode*)(node))->attrs[i])

#define IS_A(node, ty) (!VAL_IS_IMM(node) && nb_syntax_node_is((node), (ty)))

#define IS_WRAPPER(node) (!VAL_IS_IMM(node) && nb_node_is_wrapper(node))

#define TAIL(node) ((ConsNode*)(node))->list

#define HEAD(node) ((ConsNode*)(node))->e

#define COMPILE_ERROR(M, ...) printf(M, ##__VA_ARGS__); _Exit(-1)
