#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "list.h"

#define OBJ_TABLE_SIZE 	256

struct obj {
	char name[64]; 	/* key */
	int enable;
	struct hlist_node node;
};

static struct hlist_head sample_hlist[OBJ_TABLE_SIZE];

/* djb2 hash algorithm (http://www.cse.yorku.ca/~oz/hash.html) */
unsigned long hash(unsigned char *str)
{
	unsigned long hash = 5381;
	int c;

	while (c = *str++)
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

	return (hash % OBJ_TABLE_SIZE);
}


struct obj* obj_get(char *name)
{
	struct obj *obj;
	struct hlist_node *pos;
	unsigned long hashId = hash(name);

	hlist_for_each_entry(obj, pos, &sample_hlist[hashId], node) {
		if (!strcmp(obj->name, name)) {
			return obj;
		}
	}

	return NULL;
}

struct obj* obj_add(char *name)
{
	struct obj *obj;
	unsigned long hashId = hash(name);

	obj = malloc(sizeof(struct obj));
	if (obj == NULL) {
		printf("malloc error: %m");
		return NULL;
	}
	
	memset(obj, 0, sizeof(struct obj));
	snprintf(obj->name, sizeof(obj->name), "%s", name);

	INIT_HLIST_NODE(&obj->node);
	hlist_add_head(&obj->node, &sample_hlist[hashId]);

	return obj;
}

void obj_remove(char *name)
{
	struct obj *obj;
	struct hlist_node *pos, *tmp;
	unsigned long hashId = hash(name);

	hlist_for_each_entry_safe(obj, pos, tmp, &sample_hlist[hashId], node) {
		if (!strcmp(obj->name, name)) {
			hlist_del(&obj->node);
			free(obj);
		}
	}
}

void obj_hlist_show(void)
{
	struct obj *obj;
	struct hlist_node *pos;
	int i, j;

	printf("obj_hlist_show:\n");
	j = 0;
	for (i = 0; i < OBJ_TABLE_SIZE; i++) {
		hlist_for_each_entry(obj, pos, &sample_hlist[i], node) {
			printf("%3dth name:%s enable:%d hashId:%d\n", j, obj->name, obj->enable, i);
			j++;
		}
	}
}

void obj_hlist_cleanup(void)
{
	struct obj *obj;
	struct hlist_node *pos, *tmp;
	int i;

	for (i = 0; i < OBJ_TABLE_SIZE; i++) {
		hlist_for_each_entry_safe(obj, pos, tmp, &sample_hlist[i], node) {
			hlist_del(&obj->node);
			free(obj);
		}
	}
}


int main(void)
{
	int i;
	struct obj *obj;

	for (i = 0; i < OBJ_TABLE_SIZE; i++)
		INIT_HLIST_HEAD(&sample_hlist[i]);

	/* Add items to the hlist */
	obj_add("superman");
	obj_add("spiderman");
	obj = obj_add("batman");
	obj->enable = 1;
	
	/* Show the list */
	obj_hlist_show();

	/* You can use obj_get() to determine whether this object was added before */
	obj = obj_get("superman");
	if (obj != NULL)
		printf("The obj `%s' was added before\n", obj->name);

	/* Finally, we clean up the list */
	obj_hlist_cleanup();

	/* Show the list again */
	obj_hlist_show();
}
