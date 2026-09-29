/*--------------------------------------------------------------------
 *    The MB-system:  mbtrn_w32_queue.h
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * MSVC substitute for the BSD <sys/queue.h>.
 *
 * Only the two list flavours mbtrnframe/mmqueue.h actually uses are provided -
 * the tail queue (TAILQ_) and the circular queue (CIRCLEQ_) - and within those
 * only the macros it names. The definitions follow the classic 4.4BSD
 * sys/queue.h semantics so that behaviour matches the system header the other
 * platforms compile against.
 *
 * Macros supplied:
 *   TAILQ_HEAD TAILQ_ENTRY TAILQ_INIT TAILQ_EMPTY TAILQ_FIRST TAILQ_LAST
 *   TAILQ_NEXT TAILQ_PREV TAILQ_FOREACH TAILQ_FOREACH_REVERSE
 *   TAILQ_INSERT_HEAD TAILQ_INSERT_TAIL TAILQ_REMOVE
 *   CIRCLEQ_HEAD CIRCLEQ_ENTRY CIRCLEQ_INIT CIRCLEQ_EMPTY CIRCLEQ_FIRST
 *   CIRCLEQ_LAST CIRCLEQ_NEXT CIRCLEQ_PREV CIRCLEQ_END CIRCLEQ_FOREACH
 *   CIRCLEQ_INSERT_TAIL CIRCLEQ_REMOVE
 */

#ifndef MBTRN_W32_QUEUE_H
#define MBTRN_W32_QUEUE_H

/*
 * Tail queue: doubly linked, with a pointer to the last element's next field
 * so that appends and reverse traversal are O(1).
 */
#define TAILQ_HEAD(name, type)                                                 \
	struct name {                                                              \
		struct type *tqh_first;      /* first element */                       \
		struct type **tqh_last;      /* addressof last element's next */       \
	}

#define TAILQ_ENTRY(type)                                                      \
	struct {                                                                   \
		struct type *tqe_next;       /* next element */                        \
		struct type **tqe_prev;      /* addressof previous element's next */   \
	}

#define TAILQ_FIRST(head)          ((head)->tqh_first)
#define TAILQ_END(head)            NULL
#define TAILQ_EMPTY(head)          (TAILQ_FIRST(head) == TAILQ_END(head))
#define TAILQ_NEXT(elm, field)     ((elm)->field.tqe_next)

#define TAILQ_LAST(head, headname)                                             \
	(*(((struct headname *)((head)->tqh_last))->tqh_last))

#define TAILQ_PREV(elm, headname, field)                                       \
	(*(((struct headname *)((elm)->field.tqe_prev))->tqh_last))

#define TAILQ_INIT(head)                                                       \
	do {                                                                       \
		(head)->tqh_first = NULL;                                              \
		(head)->tqh_last = &(head)->tqh_first;                                 \
	} while (0)

#define TAILQ_FOREACH(var, head, field)                                        \
	for ((var) = TAILQ_FIRST(head); (var) != TAILQ_END(head);                  \
	     (var) = TAILQ_NEXT(var, field))

#define TAILQ_FOREACH_REVERSE(var, head, headname, field)                      \
	for ((var) = TAILQ_LAST(head, headname); (var) != TAILQ_END(head);         \
	     (var) = TAILQ_PREV(var, headname, field))

#define TAILQ_INSERT_HEAD(head, elm, field)                                    \
	do {                                                                       \
		if (((elm)->field.tqe_next = (head)->tqh_first) != NULL)               \
			(head)->tqh_first->field.tqe_prev = &(elm)->field.tqe_next;        \
		else                                                                   \
			(head)->tqh_last = &(elm)->field.tqe_next;                         \
		(head)->tqh_first = (elm);                                             \
		(elm)->field.tqe_prev = &(head)->tqh_first;                            \
	} while (0)

#define TAILQ_INSERT_TAIL(head, elm, field)                                    \
	do {                                                                       \
		(elm)->field.tqe_next = NULL;                                          \
		(elm)->field.tqe_prev = (head)->tqh_last;                              \
		*(head)->tqh_last = (elm);                                             \
		(head)->tqh_last = &(elm)->field.tqe_next;                             \
	} while (0)

#define TAILQ_REMOVE(head, elm, field)                                         \
	do {                                                                       \
		if (((elm)->field.tqe_next) != NULL)                                   \
			(elm)->field.tqe_next->field.tqe_prev = (elm)->field.tqe_prev;     \
		else                                                                   \
			(head)->tqh_last = (elm)->field.tqe_prev;                          \
		*(elm)->field.tqe_prev = (elm)->field.tqe_next;                        \
	} while (0)

/*
 * Circular queue: the head acts as the terminator in both directions, so the
 * end-of-list sentinel is the head itself rather than NULL.
 */
#define CIRCLEQ_HEAD(name, type)                                               \
	struct name {                                                              \
		struct type *cqh_first;      /* first element */                       \
		struct type *cqh_last;       /* last element */                        \
	}

#define CIRCLEQ_ENTRY(type)                                                    \
	struct {                                                                   \
		struct type *cqe_next;       /* next element */                        \
		struct type *cqe_prev;       /* previous element */                    \
	}

#define CIRCLEQ_FIRST(head)        ((head)->cqh_first)
#define CIRCLEQ_LAST(head)         ((head)->cqh_last)
#define CIRCLEQ_END(head)          ((void *)(head))
#define CIRCLEQ_EMPTY(head)        (CIRCLEQ_FIRST(head) == CIRCLEQ_END(head))
#define CIRCLEQ_NEXT(elm, field)   ((elm)->field.cqe_next)
#define CIRCLEQ_PREV(elm, field)   ((elm)->field.cqe_prev)

#define CIRCLEQ_INIT(head)                                                     \
	do {                                                                       \
		(head)->cqh_first = (void *)(head);                                    \
		(head)->cqh_last = (void *)(head);                                     \
	} while (0)

#define CIRCLEQ_FOREACH(var, head, field)                                      \
	for ((var) = CIRCLEQ_FIRST(head); (var) != CIRCLEQ_END(head);              \
	     (var) = CIRCLEQ_NEXT(var, field))

#define CIRCLEQ_INSERT_TAIL(head, elm, field)                                  \
	do {                                                                       \
		(elm)->field.cqe_next = (void *)(head);                                \
		(elm)->field.cqe_prev = (head)->cqh_last;                              \
		if ((head)->cqh_last == (void *)(head))                                \
			(head)->cqh_first = (elm);                                         \
		else                                                                   \
			(head)->cqh_last->field.cqe_next = (elm);                          \
		(head)->cqh_last = (elm);                                              \
	} while (0)

#define CIRCLEQ_REMOVE(head, elm, field)                                       \
	do {                                                                       \
		if ((elm)->field.cqe_next == (void *)(head))                           \
			(head)->cqh_last = (elm)->field.cqe_prev;                          \
		else                                                                   \
			(elm)->field.cqe_next->field.cqe_prev = (elm)->field.cqe_prev;     \
		if ((elm)->field.cqe_prev == (void *)(head))                           \
			(head)->cqh_first = (elm)->field.cqe_next;                         \
		else                                                                   \
			(elm)->field.cqe_prev->field.cqe_next = (elm)->field.cqe_next;     \
	} while (0)

#endif /* MBTRN_W32_QUEUE_H */
