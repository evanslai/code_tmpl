#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "list.h"

struct obj {
	char name[64]; 	/* key */
	int enable;
	struct list_head list;
};

/* The following list is used to store an object. */
static struct list_head sample_list;


struct obj* obj_get(char *name)
{
	struct obj *obj;

	list_for_each_entry(obj, &sample_list, list) {
		if (!strcmp(obj->name, name))
			return obj;
	}

	return NULL;
}

struct obj* obj_add(char *name)
{
	struct obj *obj;

	obj = malloc(sizeof(struct obj));
	if (obj == NULL) {
		printf("malloc error: %m");
		return NULL;
	}

	memset(obj, 0, sizeof(struct obj));
	snprintf(obj->name, sizeof(obj->name), "%s", name);
	list_add_tail(&obj->list, &sample_list);
	return obj;
}

void obj_remove(char *name)
{
	struct list_head *walk, *tmp;
	struct obj *obj;

	list_for_each_safe(walk, tmp, &sample_list) {
		obj = list_entry(walk, struct obj, list);
		if (!strcmp(obj->name, name)) {
			list_del(&obj->list);
			free(obj);
		}
	}
}

void obj_list_show(void)
{
	struct obj *obj;
	int i;

	i = 0;
	list_for_each_entry(obj, &sample_list, list) {
		printf("%3dth name:%s enable:%d\n", i, obj->name, obj->enable);
		i++;
	}
}

void obj_list_cleanup(void)
{
	struct list_head *walk, *tmp;
	struct obj *obj;

	list_for_each_safe(walk, tmp, &sample_list) {
		obj = list_entry(walk, struct obj, list);
		list_del(&obj->list);
		free(obj);
	}
}



int main(void)
{
	struct obj *obj;

	/* First of all, we need to initialize a list. */
	INIT_LIST_HEAD(&sample_list);

	/* Add items to the list */
	obj_add("superman");
	obj_add("spiderman");
	obj = obj_add("batman");
	obj->enable = 1;

	/* Show the list */
	obj_list_show();

	/* You can use obj_get() to determine whether this object was added before */
	obj = obj_get("superman");
	if (obj != NULL)
		printf("The obj `%s' was added before\n", obj->name);

	/* Finally, we clean up the list */
	obj_list_cleanup();

	return 0;
}
