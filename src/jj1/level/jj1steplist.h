
/**
 *
 * @file jj1steplist.h
 *
 * Part of the OpenJazz project
 *
 * @par Licence:
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 */


#ifndef _JJ1STEPLIST_H
#define _JJ1STEPLIST_H

/**
 * Step each object of a linked list of events or bullets, last first. Each
 * object's next pointer is set to what stepping the rest of the list returned
 * just before the object itself steps, as it was when each object stepped the
 * rest of the list itself, without a stack frame per object.
 *
 * @param first The first object
 * @param ticks Time
 *
 * @return What remains of the list
 */
template <class T>
T* stepList (T* first, unsigned int ticks) {

	// Kept between calls, and never freed: a destructor at exit costs the GBA RAM
	static T** objects = nullptr;
	static int capacity = 0;

	int count = 0;
	for (T* object = first; object; object = object->next) count++;

	if (count > capacity) {
		delete[] objects;
		capacity = count * 2;
		objects = new T*[capacity];
	}

	count = 0;
	for (T* object = first; object; object = object->next) objects[count++] = object;

	T* rest = nullptr;

	for (int i = count - 1; i >= 0; i--) {
		if (i < count - 1) objects[i]->next = rest;
		rest = objects[i]->step(ticks);
	}

	return rest;

}

#endif
