/*************************************************************************
This file is part of libprolog

Copyright (C) 2010 Nokia Corporation.

This library is free software; you can redistribute
it and/or modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation
version 2.1 of the License.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
USA.
*************************************************************************/



#ifndef __LIBPROLOG_ALLOCATOR__
#define __LIBPROLOG_ALLOCATOR__


extern prolog_allocator_t __allocator;


#define __MALLOC(size, file, line, func)                                  \
    (__allocator.malloc ?                                                 \
     __allocator.malloc((size), (file), (line), (func)) : malloc((size)))

#define __REALLOC(ptr, size, file, line, func)                          \
    (__allocator.realloc ?                                              \
     __allocator.realloc((ptr), (size), (file), (line), (func)) :       \
     realloc((ptr), (size)))

#define __FREE(ptr, file, line, func)                               \
    (__allocator.free ?                                             \
     __allocator.free((ptr), (file), (line), (func)) : free((ptr)))


#define ALLOC_OBJ(ptr) ({                                                \
    typeof(ptr) __ptr;                                                   \
                                                                         \
    if ((__ptr = __MALLOC(sizeof(*__ptr),                                \
                          __FILE__, __LINE__, __FUNCTION__)) != NULL) {  \
        memset(__ptr, 0, sizeof(*__ptr));                                \
        (ptr) = __ptr;                                                   \
    }                                                                   \
    __ptr; })

/* XXX TODO: change to ALLOC_ARRAY(ptr, n) for symmetricity */
#define ALLOC_ARRAY(type, n) ({                                         \
    type *__ptr;                                                        \
                                                                        \
    if ((__ptr = __MALLOC((n) * sizeof(*__ptr),                         \
                          __FILE__, __LINE__, __FUNCTION__)) != NULL)   \
        memset(__ptr, 0, (n) * sizeof(*__ptr));                         \
    __ptr; })

#define REALLOC_ARRAY(ptr, oldn, newn) ({                              \
    typeof(ptr) __ptr;                                                 \
    size_t      __o = (oldn);                                          \
    size_t      __n = (newn);                                          \
                                                                       \
    if ((ptr) == NULL)                                                 \
        __ptr = ptr = ALLOC_ARRAY(typeof(*__ptr), __n);                \
    else {                                                             \
        __ptr = __REALLOC(ptr, __n * sizeof(*__ptr),                   \
                          __FILE__, __LINE__, __FUNCTION__);           \
        if (__ptr != NULL && __n > __o) {                              \
            memset(__ptr + (__o), 0, ((__n)-(_o)) * sizeof(*__ptr));   \
            ptr = __ptr;                                               \
        }                                                              \
    }                                                                  \
    __ptr; })

#define STRDUP(s) ({                                                  \
    char   *__d, *__s = (char *)(s);                                  \
    size_t  __n = __s ? strlen(__s) : 0;                              \
                                                                      \
    if ((__d = __MALLOC(__n + 1,                                      \
                        __FILE__, __LINE__, __FUNCTION__)) != NULL) { \
        if (__n > 0)                                                  \
            memcpy(__d, __s, __n);                                    \
        __d[__n] = '\0';                                              \
    }                                                                 \
    __d; })

#define FREE(ptr) do {                                     \
        if (ptr)                                           \
            __FREE(ptr, __FILE__, __LINE__, __FUNCTION__); \
    } while (0)

#endif /* __LIBPROLOG_ALLOCATOR__ */




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
*/

