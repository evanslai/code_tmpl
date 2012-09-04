#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/queue.h>

struct obj {
	char name[64];	/* key */
	int enable;
	TAILQ_ENTRY(obj) entry;
};

/* The following list is used to store an object. */
TAILQ_HEAD(tailhead, obj) tailq_list;

struct obj* obj_get(char *name)
{
	struct obj *obj;

	for (obj = tailq_list.tqh_first; obj != NULL; obj = obj->entry.tqe_next) {
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
	TAILQ_INSERT_TAIL(&tailq_list, obj, entry);
	return obj;
}

void obj_remove(char *name)
{
	struct obj *obj;

	for (obj = tailq_list.tqh_first; obj != NULL; obj = obj->entry.tqe_next) {
		if (!strcmp(obj->name, name)) {
			TAILQ_REMOVE(&tailq_list, obj, entry);
			free(obj);
		}
	}
}

void obj_list_show(void)
{
	struct obj *obj;
	int i;

	printf("obj_list_show:\n");
	i = 0;
	for (obj = tailq_list.tqh_first; obj != NULL; obj = obj->entry.tqe_next) {
		printf("%3dth name:%s enable:%d\n", i, obj->name, obj->enable);
		i++;
	}
}

void obj_list_cleanup(void)
{
	struct obj *obj;

	for (obj = tailq_list.tqh_first; obj != NULL; obj = obj->entry.tqe_next) {
		TAILQ_REMOVE(&tailq_list, obj, entry);
		free(obj);
	}
}



int main(void)
{
	struct obj *obj;

	TAILQ_INIT(&tailq_list);

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

	/* Show the list again */
	obj_list_show();
}


