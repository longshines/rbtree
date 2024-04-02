#include "rbtree_augmented.h"

#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#define __param(type, name, init)		\
	static type name = init

// Number of nodes in the rb-tree
__param(int, nnodes, 100);
// Number of iterations modifying the rb-tree
__param(int, perf_loops, 1000);
// Number of iterations modifying and verifying the rb-tree
__param(int, check_loops, 100);

struct test_node {
	uint32_t key;
	struct rb_node rb;

	/* following fields used for testing augmented rbtree functionality */
	uint32_t val;
	uint32_t augmented;
};

static struct rb_root_cached root = RB_ROOT_CACHED;
static struct test_node *nodes = NULL;

static void insert(struct test_node *node, struct rb_root_cached *root) {
	struct rb_node **new = &root->rb_root.rb_node, *parent = NULL;
	uint32_t key = node->key;

	/* 确定新节点的放置位置 */
	while (*new) {
		parent = *new;
		if (key < rb_entry(parent, struct test_node, rb)->key)
			new = &parent->rb_left;
		else
			new = &parent->rb_right;
	}

	/* 添加新节点并重新平衡树 */
	rb_link_node(&node->rb, parent, new);
	rb_insert_color(&node->rb, &root->rb_root);
}

static void insert_cached(struct test_node *node, struct rb_root_cached *root) {
	struct rb_node **new = &root->rb_root.rb_node, *parent = NULL;
	uint32_t key = node->key;
	bool leftmost = true;

	/* 确定新节点的放置位置 */
	while (*new) {
		parent = *new;
		if (key < rb_entry(parent, struct test_node, rb)->key)
			new = &parent->rb_left;
		else {
			new = &parent->rb_right;
			leftmost = false;
		}
	}

	/* 添加新节点并重新平衡树 */
	rb_link_node(&node->rb, parent, new);
	rb_insert_color_cached(&node->rb, root, leftmost);
}

static inline void erase(struct test_node *node, struct rb_root_cached *root) {
	/* 从树中移除一个现有节点 */
	rb_erase(&node->rb, &root->rb_root);
}

static inline void erase_cached(struct test_node *node, struct rb_root_cached *root) {
	/* 从树中移除一个现有节点 */
	rb_erase_cached(&node->rb, root);
}


#define NODE_VAL(node) ((node)->val)

RB_DECLARE_CALLBACKS_MAX(static, augment_callbacks,
                         struct test_node, rb, uint32_t, augmented, NODE_VAL)

static void insert_augmented(struct test_node *node,
                             struct rb_root_cached *root) {
	struct rb_node **new = &root->rb_root.rb_node, *rb_parent = NULL;
	uint32_t key = node->key;
	uint32_t val = node->val;
	struct test_node *parent;

	/* 确定新节点的放置位置 */
	while (*new) {
		rb_parent = *new;
		parent = rb_entry(rb_parent, struct test_node, rb);
		if (parent->augmented < val)
			parent->augmented = val;
		if (key < parent->key)
			new = &parent->rb.rb_left;
		else
			new = &parent->rb.rb_right;
	}

	node->augmented = val;

	/* 添加新节点并重新平衡树 */
	rb_link_node(&node->rb, rb_parent, new);
	rb_insert_augmented(&node->rb, &root->rb_root, &augment_callbacks);
}

static void insert_augmented_cached(struct test_node *node,
                                    struct rb_root_cached *root) {
	struct rb_node **new = &root->rb_root.rb_node, *rb_parent = NULL;
	uint32_t key = node->key;
	uint32_t val = node->val;
	struct test_node *parent;
	bool leftmost = true;

	/* 确定新节点的放置位置 */
	while (*new) {
		rb_parent = *new;
		parent = rb_entry(rb_parent, struct test_node, rb);
		if (parent->augmented < val)
			parent->augmented = val;
		if (key < parent->key)
			new = &parent->rb.rb_left;
		else {
			new = &parent->rb.rb_right;
			leftmost = false;
		}
	}

	node->augmented = val;

	/* 添加新节点并重新平衡树 */
	rb_link_node(&node->rb, rb_parent, new);
	rb_insert_augmented_cached(&node->rb, root,
	                           leftmost, &augment_callbacks);
}


static void erase_augmented(struct test_node *node, struct rb_root_cached *root) {
	/* 从树中移除一个现有节点 */
	rb_erase_augmented(&node->rb, &root->rb_root, &augment_callbacks);
}

static void erase_augmented_cached(struct test_node *node,
                                   struct rb_root_cached *root) {
	/* 从树中移除一个现有节点 */
	rb_erase_augmented_cached(&node->rb, root, &augment_callbacks);
}

static void init(void) {
	int i;
	for (i = 0; i < nnodes; i++) {
		nodes[i].key = rand();
		nodes[i].val = rand();
	}
}

static bool is_red(struct rb_node *rb) {
	return !(rb->__rb_parent_color & 1);
}

static int black_path_count(struct rb_node *rb) {
	int count;
	for (count = 0; rb; rb = rb_parent(rb))
		count += !is_red(rb);
	return count;
}

static void check_postorder_foreach(int nr_nodes) {
	struct test_node *cur, *n;
	int count = 0;
	rbtree_postorder_for_each_entry_safe(cur, n, &root.rb_root, rb)
		count++;
}

static void check_postorder(int nr_nodes) {
	struct rb_node *rb;
	int count = 0;
	for (rb = rb_first_postorder(&root.rb_root); rb; rb = rb_next_postorder(rb))
		count++;
}

static void check(int nr_nodes) {
	struct rb_node *rb;
	int count = 0, blacks = 0;
	uint32_t prev_key = 0;

	for (rb = rb_first(&root.rb_root); rb; rb = rb_next(rb)) {
		struct test_node *node = rb_entry(rb, struct test_node, rb);

		if (!count)
			blacks = black_path_count(rb);

		prev_key = node->key;
		count++;
	}

	check_postorder(nr_nodes);
	check_postorder_foreach(nr_nodes);
}

static void check_augmented(int nr_nodes) {
	struct rb_node *rb;

	check(nr_nodes);
	for (rb = rb_first(&root.rb_root); rb; rb = rb_next(rb)) {
		struct test_node *node = rb_entry(rb, struct test_node, rb);
		uint32_t subtree, max = node->val;
		if (node->rb.rb_left) {
			subtree = rb_entry(node->rb.rb_left, struct test_node,
			                   rb)->augmented;
			if (max < subtree)
				max = subtree;
		}
		if (node->rb.rb_right) {
			subtree = rb_entry(node->rb.rb_right, struct test_node,
			                   rb)->augmented;
			if (max < subtree)
				max = subtree;
		}
	}
}

int64_t get_timestamp() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

int64_t time_diff(int64_t start, int64_t end) {
	return (end - start);
}

int main(void) {
	int i, j;
	int64_t time1, time2, time_d;
	struct rb_node *node;

	nodes = (struct test_node *) malloc(sizeof(struct test_node) * nnodes);
	if (!nodes)
		return -ENOMEM;

	printf("rbtree testing\n");

	srand(time(NULL));
	init();

	time1 = get_timestamp();

	for (i = 0; i < perf_loops; i++) {
		for (j = 0; j < nnodes; j++)
			insert(nodes + j, &root);
		for (j = 0; j < nnodes; j++)
			erase(nodes + j, &root);
	}

	time2 = get_timestamp();

	time_d = time_diff(time1, time2);
	printf(" -> test 1 (latency of nnodes insert+delete): %llu us\n",
	       (unsigned long long) time_d);

	time1 = get_timestamp();

	for (i = 0; i < perf_loops; i++) {
		for (j = 0; j < nnodes; j++)
			insert_cached(nodes + j, &root);
		for (j = 0; j < nnodes; j++)
			erase_cached(nodes + j, &root);
	}

	time2 = get_timestamp();

	time_d = time_diff(time1, time2);
	printf(" -> test 2 (latency of nnodes cached insert+delete): %llu us\n",
	       (unsigned long long) time_d);

	for (i = 0; i < nnodes; i++)
		insert(nodes + i, &root);

	time1 = get_timestamp();

	for (i = 0; i < perf_loops; i++) {
		for (node = rb_first(&root.rb_root); node; node = rb_next(node));
	}

	time2 = get_timestamp();

	time_d = time_diff(time1, time2);
	printf(" -> test 3 (latency of inorder traversal): %llu us\n",
	       (unsigned long long) time_d);

	time1 = get_timestamp();

	for (i = 0; i < perf_loops; i++)
		node = rb_first(&root.rb_root);

	time2 = get_timestamp();

	time_d = time_diff(time1, time2);
	printf(" -> test 4 (latency to fetch first node)\n");
	printf("        non-cached: %llu us\n", (unsigned long long) time_d);

	time1 = get_timestamp();

	for (i = 0; i < perf_loops; i++)
		node = rb_first_cached(&root);

	time2 = get_timestamp();

	time_d = time_diff(time1, time2);
	printf("        cached: %llu us\n", (unsigned long long) time_d);

	for (i = 0; i < nnodes; i++)
		erase(nodes + i, &root);

	/* run checks */
	for (i = 0; i < check_loops; i++) {
		init();
		for (j = 0; j < nnodes; j++) {
			check(j);
			insert(nodes + j, &root);
		}
		for (j = 0; j < nnodes; j++) {
			check(nnodes - j);
			erase(nodes + j, &root);
		}
		check(0);
	}

	printf("augmented rbtree testing\n");

	init();

	time1 = get_timestamp();

	for (i = 0; i < perf_loops; i++) {
		for (j = 0; j < nnodes; j++)
			insert_augmented(nodes + j, &root);
		for (j = 0; j < nnodes; j++)
			erase_augmented(nodes + j, &root);
	}

	time2 = get_timestamp();

	time_d = time_diff(time1, time2);
	printf(" -> test 1 (latency of nnodes insert+delete): %llu us\n", (unsigned long long) time_d);

	time1 = get_timestamp();

	for (i = 0; i < perf_loops; i++) {
		for (j = 0; j < nnodes; j++)
			insert_augmented_cached(nodes + j, &root);
		for (j = 0; j < nnodes; j++)
			erase_augmented_cached(nodes + j, &root);
	}

	time2 = get_timestamp();

	time_d = time_diff(time1, time2);
	printf(" -> test 2 (latency of nnodes cached insert+delete): %llu us\n", (unsigned long long) time_d);

	for (i = 0; i < check_loops; i++) {
		init();
		for (j = 0; j < nnodes; j++) {
			check_augmented(j);
			insert_augmented(nodes + j, &root);
		}
		for (j = 0; j < nnodes; j++) {
			check_augmented(nnodes - j);
			erase_augmented(nodes + j, &root);
		}
		check_augmented(0);
	}

	free(nodes);

	return 0;
}
