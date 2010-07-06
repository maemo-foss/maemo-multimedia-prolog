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



#ifndef __LIST_HOOK_H__
#define __LIST_HOOK_H__

#ifdef __cplusplus
extern "C" {
#endif


/*
 * doubly linked lists, obviously inspired by the linux kernel implementation
 */

#define LIST_HOOK(name) list_hook_t name = { &(name), &(name) }

typedef struct list_hook list_hook_t;

struct list_hook {
	list_hook_t *prev;
	list_hook_t *next;
};


/********************
 * list_hook_init
 ********************/
static inline void
list_hook_init(list_hook_t *list)
{
	list->prev = list->next = list;
}


/********************
 * list_is_empty
 ********************/
static inline int
list_is_empty(list_hook_t *list)
{
	return (list->next == list) /* && (list->prev == list) */;
}


/********************
 * list_add_head
 ********************/
static inline void
list_add_head(list_hook_t *e, list_hook_t *list)
{
	list_hook_t *next = list->next;
	
	/*
	 * insert e after the head of the list (ie. between list and next)
	 */

	if (list_is_empty(list)) {
		list->next = e;
		e->next    = list;
		e->prev    = list;
		list->prev = e;
	}
	else {
		list->next = e;
		e->prev    = list;
		e->next    = next;
		next->prev = e;
	}
}


/********************
 * list_add_tail
 ********************/
static inline void
list_add_tail(list_hook_t *e, list_hook_t *list)
{
	list_hook_t *prev = list->prev;

	/*
	 * insert e at the tail (ie. before the head, ie. between prev and list)
	 */

	if (list_is_empty(list)) {
		list->next = e;
		e->next    = list;
		e->prev    = list;
		list->prev = e;
	}
	else {
		prev->next = e;
		e->prev    = prev;
		e->next    = list;
		list->prev = e;
	}
}


/********************
 * list_delete
 ********************/
static inline void
list_delete(list_hook_t *e)
{
	list_hook_t *prev = e->prev;
	struct list_hook *next = e->next;

	prev->next = next;
	next->prev = prev;
}


/********************
 * list_entry
 ********************/
#define __MEMBER_OFFSET(type, member) ((size_t)&((type *)NULL)->member)
#define list_entry(p, type, member) \
    (type *)(((char *)p) - __MEMBER_OFFSET(type, member))


/********************
 * list_iter_forw
 ********************/
#define list_iter_forw(p, list) \
    for ((p) = (list)->next; (p) != (list); (p) = (p)->next)


/********************
 * list_iter_back
 ********************/
#define list_iter_back(p, list) \
    for ((p) = (list)->prev; (p) != (list); (p) = (p)->prev)


/********************
 * list_iter_forw_safe
 ********************/
#define list_iter_forw_safe(p, n, list) \
    for ((p) = (list)->next, (n) = (p)->next; \
		 (p) != (list); \
         (p) = (n), (n) = (n)->next)


/********************
 * list_iter_back_safe
 ********************/
#define list_iter_back_safe(p, n, list) \
    for ((p) = (list)->prev, (n) = (p)->prev; \
         (p) != (list); \
         (p) = (n), (n) = (n)->prev)


#ifdef __cplusplus
}
#endif


#endif /* __LIST_HOOK_H__ */


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
