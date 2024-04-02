[English](./README.md)

# 红黑树（rbtree）
rbtree lib是基于Linux kernel v6.8.2中的相关源码进行修改，可在用户态程序中使用。

## 什么是红黑树，可以用来做什么？
红黑树是一种自平衡二叉搜索树，用于存储可排序的键/值数据对。它不同于基数树(radix tree，用于高效存储稀疏数组，使用长整数索引来插入/访问/删除节
点）和哈希表（无法按序遍历，且必须针对数据规模和哈希函数进行调优，而 rbtrees 则可从容地扩展存储任意key）。

红黑树与 AVL 树类似，但在最坏情况下能提供更快的实时有界性能的插入和删除操作（最多分别旋转两次和三次），而查找时间稍慢（但仍为 $O(log n)$）。

## 红黑树的实现
rbtree的实现位于文件`lib/rbtree.c`中。使用时，请include `rbtree.h`。

rbtree的实现针对速度进行了优化，比传统树的实现少了一层间接引用（因此具有更好的缓存局部性）。它不是使用指针指向分离的`rb_node`和数据结构，
而是每个`struct rb_node`的实例都嵌入在它组织的数据结构中。并且，它不使用比较回调函数指针，用户应该编写自己的**搜索和插入**函数，这些函数再
调用已提供的rbtree相关函数。Lock也同样留给rbtree的使用者来处理。

## 创建一个新的红黑树
红黑树中的数据节点是包含`struct rb_node`成员的结构：

``` C
struct mytype {
    struct rb_node node;
    char *keystring;
};
```

处理嵌入的`struct rb_node`的指针时，可以使用`container_of()`宏访问包含`struct rb_node`的数据结构。此外，还可以通过
`rb_entry(node, type, member)`直接访问个别成员。

每个红黑树的根都是一个`rb_root`结构，可以通过以下方式进行初始化：

``` C
struct rb_root mytree = RB_ROOT;
```

## 在红黑树中搜索一个值
编写树的搜索函数相当直接：从根开始，比较每个值，根据需要沿左或右分支进行。

示例：
``` C
struct mytype *my_search(struct rb_root *root, char *string)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct mytype *data = container_of(node, struct mytype, node);
		int result;

		result = strcmp(string, data->keystring);

		if (result < 0)
			node = node->rb_left;
		else if (result > 0)
			node = node->rb_right;
		else
			return data;
	}
	return NULL;
}
```

## 将数据插入红黑树
将数据插入树中首先需要搜索插入新节点的位置，然后插入节点并重新平衡（“重新着色”）树。

插入搜索与之前的搜索不同，它可以找到指针的位置，将新节点嫁接到该指针上。新节点还需要一个指向其父节点的链接，以便重新平衡。

示例：

``` C
int my_insert(struct rb_root *root, struct mytype *data)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	/* 确定新节点的放置位置 */
	while (*new) {
		struct mytype *this = container_of(*new, struct mytype, node);
		int result = strcmp(data->keystring, this->keystring);

		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else
			return FALSE;
	}

	/* 添加新节点并重新平衡树 */
	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);

	return TRUE;
}
```

## 在红黑树中移除或替换已存在的数据
要从树中移除一个现有节点，请调用：
``` C
void rb_erase(struct rb_node *victim, struct rb_root *tree);
```

示例：

``` C
struct mytype *data = mysearch(&mytree, "walrus");

if (data) {
	rb_erase(&data->node, &mytree);
	myfree(data);
}
```

要用一个具有相同键的新节点替换树中的现有节点，请调用：

``` C
void rb_replace_node(struct rb_node *old, struct rb_node *new,
	struct rb_root *tree);
```

这种节点替换操作不会对树重新排序：如果新节点没有与旧节点相同的键，红黑树很可能会遭到破坏。

## 按序遍历存储在红黑树中的元素
有四个函数被用于按序遍历红黑树的内容。这些函数适用于任意树，不需要对其修改或封装（除了Lock目的之外）：

``` C
struct rb_node *rb_first(struct rb_root *tree);
struct rb_node *rb_last(struct rb_root *tree);
struct rb_node *rb_next(struct rb_node *node);
struct rb_node *rb_prev(struct rb_node *node);
```

开始遍历时，用指向根的指针调用`rb_first()`或`rb_last()`，这将返回一个指向树中第一个或最后一个元素中包含的节点结构的指针。要继续遍历，通过对
当前节点调用`rb_next()`或`rb_prev()`来获取下一个或上一个节点。当没有更多节点时，将返回NULL。

迭代器函数返回一个指向嵌入的`struct rb_node`的指针，可以使用`container_of()`宏访问包含`struct rb_node`的数据结构，并且可以通过
`rb_entry(node, type, member)`直接访问个别成员。

示例：

``` C
struct rb_node *node;
for (node = rb_first(&mytree); node; node = rb_next(node))
	printf("key=%s\n", rb_entry(node, struct mytype, node)->keystring);
```

## 缓存红黑树
计算最左边（最小）的节点是二叉搜索树的一项常见任务，例如用于遍历或用户依赖于特定顺序来实现他们自己的逻辑。为此，用户可以使用 
`struct rb_root_cached`来优化 $O(logN)$的`rb_first()` 调用，使其成为一个简单的指针取值，从而避免潜在的昂贵的树迭代。尽管内存占用较大，
但运行时的维护开销可以忽略不计。

与`rb_root`结构类似，缓存红黑树通过以下方式初始化为空：

``` C
struct rb_root_cached mytree = RB_ROOT_CACHED;
```

缓存红黑树只是普通的 `rb_root` 加上一个额外的指针来缓存最左边的节点。这使得 `rb_root_cached` 可以存在于 `rb_root` 的任何地方，
从而只需几个额外的接口就可以支持：

``` C
struct rb_node *rb_first_cached(struct rb_root_cached *tree);
void rb_insert_color_cached(struct rb_node *, struct rb_root_cached *, bool);
void rb_erase_cached(struct rb_node *node, struct rb_root_cached *);
```

`insert`和`erase`调用都有其增强树的对应版本：

``` C
void rb_insert_augmented_cached(struct rb_node *node, struct rb_root_cached *,
	bool, struct rb_augment_callbacks *);
void rb_erase_augmented_cached(struct rb_node *, struct rb_root_cached *,
	struct rb_augment_callbacks *);
```

## 增强型红黑树的支持
增强型红黑树是一种在每个节点中存储了“某些”附加数据的红黑树，其中节点N的附加数据必须是以N为根的子树中所有节点内容的函数。这些数据可以用来为
红黑树增加一些新功能。增强型红黑树是建立在基础红黑树基础设施之上的可选功能。希望使用此功能的红黑树用户必须在插入和删除节点时调用带有用户提供的
增强回调的增强函数。

实现增强型红黑树操作的C文件必须包含`rbtree_augmented.h`而不是`rbtree.h`。请注意，`rbtree_augmented.h`暴露了一些您不应该依赖的红黑树
实现细节；请坚持使用带有document的API，也不要从头文件中包含`rbtree_augmented.h`，以尽量减少用户意外依赖这些实现细节的机会。

在插入时，用户必须更新通往插入节点路径上的增强信息，然后像往常一样调用`rb_link_node()`，并调用`rb_augment_inserted()`而不是
`rb_insert_color()`。如果`rb_augment_inserted()`重新平衡了红黑树，它将回调到用户提供的函数中，以更新受影响的子树上的增强信息。

删除节点时，用户必须调用`rb_erase_augmented()`而不是`rb_erase()`。`rb_erase_augmented()`回调到用户提供的函数中，以更新受影响的子树上
的增强信息。

在这两种情况下，都通过`struct rb_augment_callbacks`提供回调。因此，必须定义3个回调：

- 一个`propagation`回调，它更新给定节点及其祖先的增强值，直到给定的停止点（或NULL以更新到根）。
- 一个`copy`回调，它将给定子树的增强值复制到新指定的子树根。
- 一个`tree rotation`回调，它将给定子树的增强值复制到新指定的子树根，并重新计算前子树根的增强信息。

`rb_erase_augmented()`的编译代码可能会内联`propagation`和`copy`回调，这会导致形成一个大型函数，因此每个增强型红黑树用户应该在单个地方调用
`rb_erase_augmented()`，以限制编译后代码的大小。

### 示例用法
区间树是增强型红黑树的一个例子。（参考《算法导论》（作者Cormen, Leiserson, Rivest和Stein））。关于区间树的更多细节：

经典红黑树有一个单一的键，它不能直接用来存储像\[lo:hi\]这样的区间范围，并对新的lo:hi进行快速查找以查看是否有任何重叠，或者找到新的lo:hi的精确匹配。

然而，增强红黑树可以以结构化的方式存储这样的区间范围，以进行有效的查找和精确匹配。

存储在每个节点中的“额外信息”是其所有后代节点中最大的hi（`max_hi`）值。只需查看节点及其直接子节点就可以维护每个节点的这些信息。并且这将用于
$O(logn)$查找与类似的最低匹配（在所有可能匹配中最低开始地址）：

``` C
struct interval_tree_node *
	interval_tree_first_match(struct rb_root *root,
				  unsigned long start, unsigned long last)
{
	struct interval_tree_node *node;

	if (!root->rb_node)
		return NULL;
	node = rb_entry(root->rb_node, struct interval_tree_node, rb);

	while (true) {
		if (node->rb.rb_left) {
			struct interval_tree_node *left =
				rb_entry(node->rb.rb_left,
					 struct interval_tree_node, rb);
			if (left->__subtree_last >= start) {
				/*
				 * 左子树中的某些节点满足 Cond2。
				 * 遍历找到最左边的节点 N。
				 * 如果它也满足 Cond1，那就是我们要找的匹配节点。
				 * 否则，没有匹配区间，因为 N 右边的节点也不能满足 Cond1。
				 */
				 
				node = left;
				continue;
			}
		}
		if (node->start <= last) {      /* Cond1 */
			if (node->last >= start)    /* Cond2 */
				return node;    /* 匹配最左的节点 */
			if (node->rb.rb_right) {
				node = rb_entry(node->rb.rb_right,
					struct interval_tree_node, rb);
				if (node->__subtree_last >= start)
					continue;
			}
		}
		return NULL;    /* 未找到匹配 */
	}
}
```

插入/移除使用以下增强回调定义：

``` C
static inline unsigned long
    compute_subtree_last(struct interval_tree_node *node)
    {
      unsigned long max = node->last, subtree_last;
      if (node->rb.rb_left) {
          subtree_last = rb_entry(node->rb.rb_left,
              struct interval_tree_node, rb)->__subtree_last;
          if (max < subtree_last)
              max = subtree_last;
      }
      if (node->rb.rb_right) {
          subtree_last = rb_entry(node->rb.rb_right,
              struct interval_tree_node, rb)->__subtree_last;
          if (max < subtree_last)
              max = subtree_last;
      }
      return max;
    }

static void augment_propagate(struct rb_node *rb, struct rb_node *stop) {
	while (rb != stop) {
		struct interval_tree_node *node =
				rb_entry(rb, struct interval_tree_node, rb);
		unsigned long subtree_last = compute_subtree_last(node);
		if (node->__subtree_last == subtree_last)
			break;
		node->__subtree_last = subtree_last;
		rb = rb_parent(&node->rb);
	}
}

static void augment_copy(struct rb_node *rb_old, struct rb_node *rb_new) {
	struct interval_tree_node *old =
			rb_entry(rb_old, struct interval_tree_node, rb);
	struct interval_tree_node *new =
			rb_entry(rb_new, struct interval_tree_node, rb);

	new->__subtree_last = old->__subtree_last;
}

static void augment_rotate(struct rb_node *rb_old, struct rb_node *rb_new) {
	struct interval_tree_node *old =
			rb_entry(rb_old, struct interval_tree_node, rb);
	struct interval_tree_node *new =
			rb_entry(rb_new, struct interval_tree_node, rb);

	new->__subtree_last = old->__subtree_last;
	old->__subtree_last = compute_subtree_last(old);
}

static const struct rb_augment_callbacks augment_callbacks = {
	augment_propagate, augment_copy, augment_rotate
};

void interval_tree_insert(struct interval_tree_node *node,
                          struct rb_root *root) {
	struct rb_node **link = &root->rb_node, *rb_parent = NULL;
	unsigned long start = node->start, last = node->last;
	struct interval_tree_node *parent;

	while (*link) {
		rb_parent = *link;
		parent = rb_entry(rb_parent, struct interval_tree_node, rb);
		if (parent->__subtree_last < last)
			parent->__subtree_last = last;
		if (start < parent->start)
			link = &parent->rb.rb_left;
		else
			link = &parent->rb.rb_right;
	}

	node->__subtree_last = last;
	rb_link_node(&node->rb, rb_parent, link);
	rb_insert_augmented(&node->rb, root, &augment_callbacks);
}

void interval_tree_remove(struct interval_tree_node *node,
                          struct rb_root *root) {
	rb_erase_augmented(&node->rb, root, &augment_callbacks);
}
```

## License
源文件均基于GPL-2.0-or-later (请查阅 [LICENSE](./LICENSE)).