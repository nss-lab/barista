/*
 * Copyright 2015-2018 NSSLab, KAIST
 */

/**
 * \file
 * \author Jaehyun Nam <namjh@kaist.ac.kr>
 */

// inside of 'conflict.c'

/////////////////////////////////////////////////////////////////////

/** \brief The number of pre-allocated flows */
#define ARR_PRE_ALLOC 65536

/** \brief The structure of a flow pool */
typedef struct _arr_queue_t {
    int size; /**< The size of entries */

    flow_t *head; /**< The head pointer */
    flow_t *tail; /**< The tail pointer */

    pthread_spinlock_t lock; /**< The lock for management */
} arr_queue_t;

/** \brief Flow pool */
arr_queue_t arr_q;

/////////////////////////////////////////////////////////////////////

/**
 * \brief Function to pop a flow from a flow pool
 * \return Empty flow
 */
static flow_t *arr_dequeue(void)
{
    flow_t *new = NULL;

    pthread_spin_lock(&arr_q.lock);

    if (arr_q.head == NULL) {
        pthread_spin_unlock(&arr_q.lock);

        new = (flow_t *)CALLOC(1, sizeof(flow_t));
        if (new == NULL) {
            PERROR("calloc");
            return NULL;
        }

        pthread_spin_lock(&arr_q.lock);
        arr_q.size--;
        pthread_spin_unlock(&arr_q.lock);

        return new;
    } else if (arr_q.head == arr_q.tail) {
        new = arr_q.head;
        arr_q.head = arr_q.tail = NULL;
    } else {
        new = arr_q.head;
        arr_q.head = arr_q.head->next;
        arr_q.head->prev = NULL;
    }

    arr_q.size--;

    pthread_spin_unlock(&arr_q.lock);

    memset(new, 0, sizeof(flow_t));

    return new;
}

/**
 * \brief Function to push a used flow into a flow pool
 * \param flow Used flow
 */
static int arr_enqueue(flow_t *flow)
{
    if (flow == NULL)
        return -1;

    memset(flow, 0, sizeof(flow_t));

    pthread_spin_lock(&arr_q.lock);

    if (arr_q.size < 0) {
        arr_q.size++;

        pthread_spin_unlock(&arr_q.lock);

        FREE(flow);

        return 0;
    }

    if (arr_q.head == NULL && arr_q.tail == NULL) {
        arr_q.head = arr_q.tail = flow;
    } else {
        arr_q.tail->next = flow;
        flow->prev = arr_q.tail;
        arr_q.tail = flow;
    }

    arr_q.size++;

    pthread_spin_unlock(&arr_q.lock);

    return 0;
}

/**
 * \brief Function to initialize a flow pool
 * \return None
 */
static int arr_q_init(void)
{
    memset(&arr_q, 0, sizeof(arr_queue_t));

    pthread_spin_init(&arr_q.lock, PTHREAD_PROCESS_PRIVATE);

    int i;
    for (i=0; i<ARR_PRE_ALLOC; i++) {
        flow_t *new = (flow_t *)MALLOC(sizeof(flow_t));
        if (new == NULL) {
            PERROR("malloc");
            return -1;
        }

        arr_enqueue(new);
    }

    return 0;
}

/**
 * \brief Function to destroy a flow pool
 * \return None
 */
static int arr_q_destroy(void)
{
    pthread_spin_lock(&arr_q.lock);

    flow_t *curr = arr_q.head;
    while (curr != NULL) {
        flow_t *tmp = curr;
        curr = curr->next;
        if (tmp)
            FREE(tmp);
    }

    pthread_spin_unlock(&arr_q.lock);

    pthread_spin_destroy(&arr_q.lock);

    return 0;
}
