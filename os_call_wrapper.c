/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "bh_common.h"
#include "bh_log.h"
#include "wasm_export.h"
#include "../interpreter/wasm.h"

#if defined(_WIN32) || defined(_WIN32_)
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif

void
wasm_runtime_set_exception(wasm_module_inst_t module, const char *exception);

uint32
wasm_runtime_get_temp_ret(wasm_module_inst_t module);

void
wasm_runtime_set_temp_ret(wasm_module_inst_t module, uint32 temp_ret);

uint32
wasm_runtime_get_llvm_stack(wasm_module_inst_t module);

void
wasm_runtime_set_llvm_stack(wasm_module_inst_t module, uint32 llvm_stack);

uint32
wasm_runtime_module_realloc(wasm_module_inst_t module, uint32 ptr,
                            uint32 size, void **p_native_addr);

#define get_module_inst(exec_env) \
    wasm_runtime_get_module_inst(exec_env)

#define validate_app_addr(offset, size) \
    wasm_runtime_validate_app_addr(module_inst, offset, size)

#define validate_app_str_addr(offset) \
    wasm_runtime_validate_app_str_addr(module_inst, offset)

#define validate_native_addr(addr, size) \
    wasm_runtime_validate_native_addr(module_inst, addr, size)

#define addr_app_to_native(offset) \
    wasm_runtime_addr_app_to_native(module_inst, offset)

#define addr_native_to_app(ptr) \
    wasm_runtime_addr_native_to_app(module_inst, ptr)

#define module_malloc(size, p_native_addr) \
    wasm_runtime_module_malloc(module_inst, size, p_native_addr)

#define module_free(offset) \
    wasm_runtime_module_free(module_inst, offset)

typedef int (*out_func_t)(int c, void *ctx);

typedef enum parameter_type {
    PARAMETER_TYPE_VOID,
    PARAMETER_TYPE_INT8,
    PARAMETER_TYPE_INT16,
    PARAMETER_TYPE_INT32,
    PARAMETER_TYPE_INT64,
    PARAMETER_TYPE_MAX = 63
} parameter_type;

#define PARAMETER_TYPE_PTR_MASK 0xC0
 
#define PARAMETER_ANNOTATION_VOID   0x00
#define PARAMETER_ANNOTATION_IN     0x01
#define PARAMETER_ANNOTATION_OUT    0x02
#define PARAMETER_ANNOTATION_OPT    0x04
#define PARAMETER_ANNOTATION_BCOUNT 0x08
#define PARAMETER_ANNOTATION_ECOUNT 0x10
#define PARAMETER_ANNOTATION_PART   0x20
#define PARAMETER_ANNOTATION_INOUT  (PARAMETER_ANNOTATION_IN | PARAMETER_ANNOTATION_OUT)

typedef enum calling_conventions {
    CC_cdecl,
    CC_stdcall,
    CC_fastcall,
} calling_conventions;

typedef struct function_node {
    void * m_node;
    void * func;
    uint32 parameters[16];
    int parameter_count;
} function_node;

typedef struct module_node {
    HashMap * functions;
    void * base;
} module_node;

static HashMap * g_modules;

typedef char *_va_list;
#define _INTSIZEOF(n)       \
    (((uint32)sizeof(n) +  3) & (uint32)~3)
#define _va_arg(ap, t)      \
    (*(t*)((ap += _INTSIZEOF(t)) - _INTSIZEOF(t)))

#define CHECK_VA_ARG(ap, t) do {                        \
    if ((uint8*)ap + _INTSIZEOF(t) > native_end_addr)   \
        goto fail;                                      \
} while (0)

static uint32 wwrap_hash(const void *key)
{
    uint32 hash = (uint32)(uintptr_t)key;
    return hash;
}

static bool wwrap_equal(void *key1, void *key2)
{
    uint32 hash1 = (uint32)(uintptr_t)key1;
    uint32 hash2 = (uint32)(uintptr_t)key2;
    return hash1 == hash2;
}

static void module_node_free(void * value)
{
    module_node * node = value;
    if(node->functions) {
        bh_hash_map_destroy(node->functions);
    }
    wasm_runtime_free(value);
}


static uint32 wwrap_wrapper(wasm_exec_env_t exec_env, const char * module, const char * function, uint32 return_code, uint32 parameter_count, _va_list va_args)
{
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    uint8 *native_end_addr;
    module_node * m_node;
    function_node * node;
    void * base;
    void * func;
    int parameter_count;

    /* format has been checked by runtime */
    if (!validate_native_addr(module, sizeof(int32)) 
        || (((uintptr_t)function > 0xFFFF) && !validate_native_addr(function, sizeof(int32)))
        || !validate_native_addr(va_args, sizeof(int32)) ) {
        return NULL;
    }

    if (!wasm_runtime_get_native_addr_range(module_inst, (uint8*)va_args, NULL, &native_end_addr))
        goto fail;

#ifdef _WIN32    
    base = GetModuleHandleA(module);
    if(!base) {
        base = LoadLibraryA(module);
        if(!base) {
            return NULL;
        }
    }

    func = GetProcAddress(base, function);
    if(!func) {
        return NULL;
    }

#endif

    if(!g_modules) {
        g_modules = bh_hash_map_create(256, true, wwrap_hash, wwrap_equal, NULL, module_node_free);
    }

    if(!g_modules) {
        return NULL;
    }

    m_node = bh_hash_map_find(g_modules, base);
    if(!m_node) {
        m_node = wasm_runtime_malloc(sizeof(*m_node));

        if(m_node) {
            bh_hash_map_insert(g_modules, base, m_node);
        }
    }

    if(!m_node) {
        return NULL;
    }

    if(!m_node->functions) {
        m_node->functions = bh_hash_map_create(256, true, wwrap_hash, wwrap_equal, NULL, module_node_free);
    }

    if(!m_node->functions) {
        return NULL;
    }

    node = bh_hash_map_find(m_node->functions, func);
    if(node) {
        return addr_native_to_app(node);
    }

    if(!node) {
        node = wasm_runtime_malloc(sizeof(*node));
        if(node) {
            bh_hash_map_insert(m_node->functions, func, node);
        }
    }

    if(!node) {
        return NULL;
    }
    
    node->m_node = m_node;
    node->func = func;
    node->parameter_count = 0;

    node->parameters[node->parameter_count++] = return_code;

    while(parameter_count--) {
        CHECK_VA_ARG(va_args, int32);
        node->parameters[node->parameter_count++] = _va_arg(va_args, int32);
    }

    return addr_native_to_app(node);

fail:
    wasm_runtime_set_exception(module_inst, "out of bounds memory access");
    return NULL;
}

static int wunwrap_wrapper(wasm_exec_env_t exec_env, uint32 node_addr)
{
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    module_node * m_node;
    function_node * node;

    node = wasm_runtime_addr_app_to_native(module_inst, node_addr);
    if(!node) {
        return -1;
    }

    m_node = node->m_node;

    bh_hash_map_remove(m_node->functions, node->func, NULL, NULL);

    wasm_runtime_free(node);

    if(bh_hash_map_get_count(m_node->functions)) {
        return 0;
    }

    bh_hash_map_remove(g_modules, m_node->base, NULL, NULL);

    FreeLibrary(m_node->base);

    module_node_free(m_node);

    return 0;
}



static int wcall_wrapper(wasm_exec_env_t exec_env, uint32_t ref_idx, _va_list va_args)
{
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    uint8 *native_end_addr;

    /* format has been checked by runtime */
    if (!validate_native_addr(va_args, sizeof(int32)))
        return 0;

    


    return 0;
}

static uint32
malloc_wrapper(wasm_exec_env_t exec_env, uint32 size)
{
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    return module_malloc(size, NULL);
}

static uint32
calloc_wrapper(wasm_exec_env_t exec_env, uint32 nmemb, uint32 size)
{
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    uint64 total_size = (uint64) nmemb * (uint64) size;
    uint32 ret_offset = 0;
    uint8 *ret_ptr;

    if (total_size >= UINT32_MAX)
        return 0;

    ret_offset = module_malloc((uint32)total_size, (void**)&ret_ptr);
    if (ret_offset) {
        memset(ret_ptr, 0, (uint32)total_size);
    }

    return ret_offset;
}

static uint32
realloc_wrapper(wasm_exec_env_t exec_env, uint32 ptr, uint32 new_size)
{
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    return wasm_runtime_module_realloc(module_inst, ptr, new_size, NULL);
}

static void
free_wrapper(wasm_exec_env_t exec_env, void *ptr)
{
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (!validate_native_addr(ptr, sizeof(uint32)))
        return;

    module_free(addr_native_to_app(ptr));
}


static void
abort_wrapper(wasm_exec_env_t exec_env, int32 code)
{
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    char buf[32];
    snprintf(buf, sizeof(buf), "os.abort(%i)", code);
    wasm_runtime_set_exception(module_inst, buf);
}


#define REG_NATIVE_FUNC(func_name, signature)  \
    { #func_name, func_name##_wrapper, signature, NULL }

static NativeSymbol native_symbols_os_call[] = {
    REG_NATIVE_FUNC(malloc, "(i)i"),
    REG_NATIVE_FUNC(realloc, "(ii)i"),
    REG_NATIVE_FUNC(calloc, "(ii)i"),
    REG_NATIVE_FUNC(free, "(*)"),
    REG_NATIVE_FUNC(abort, "(i)"),
    REG_NATIVE_FUNC(wwrap, "($$ii*)i"),
    REG_NATIVE_FUNC(wcall, "(ii*)i"),
    REG_NATIVE_FUNC(wunwrap, "(i)i"),
};

uint32
get_os_call_export_apis(NativeSymbol **p_os_call_apis)
{
    *p_os_call_apis = native_symbols_os_call;
    return sizeof(native_symbols_os_call) / sizeof(NativeSymbol);
}



