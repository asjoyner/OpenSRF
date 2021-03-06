#ifndef OSRF_BIG_LIST_H
#define OSRF_BIG_LIST_H

#include <stdio.h>
#include <opensrf/utils.h>
#include <Judy.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
  Items are stored as void*'s so it's up to the user to
  manage the data wisely.  Also, if the 'freeItem' callback is defined for the list,
  then, it will be used on any item that needs to be freed, so don't mix data
  types in the list if you want magic freeing */

struct __osrfBigListStruct {
	Pvoid_t list;							/* the list */
	int size;								/* how many items in the list including NULL items between non-NULL items */	
	void (*freeItem) (void* item);	/* callback for freeing stored items */
};
typedef struct __osrfBigListStruct osrfBigList;


struct __osrfBigBigListIteratorStruct {
	osrfBigList* list;
	unsigned long current;
};
typedef struct __osrfBigBigListIteratorStruct osrfBigBigListIterator;


/**
  Creates a new list iterator with the given list
  */
osrfBigBigListIterator* osrfNewBigListIterator( osrfBigList* list );

/**
  Returns the next non-NULL item in the list, return NULL when
  the end of the list has been reached
  */
void* osrfBigBigListIteratorNext( osrfBigBigListIterator* itr );

/**
  Deallocates the given list
  */
void osrfBigBigListIteratorFree( osrfBigBigListIterator* itr );

void osrfBigBigListIteratorReset( osrfBigBigListIterator* itr );


/**
  Allocates a new list
  @param compress If true, the list will compress empty slots on delete.  If item positionality
  is not important, then using this feature is reccomended to keep the list from growing indefinitely.
  if item positionality is not important.
  @return The allocated list
  */
osrfBigList* osrfNewBigList();

/**
  Pushes an item onto the end of the list.  This always finds the highest index
  in the list and pushes the new item into the list after it.
  @param list The list
  @param item The item to push
  @return 0 on success, -1 on failure
  */
int osrfBigListPush( osrfBigList* list, void* item );


/**
 * Removes the last item in the list
 * See osrfBigListRemove for details on how the removed item is handled
 * @return The item, unless 'freeItem' exists, then returns NULL
 */
void* osrfBigListPop( osrfBigList* list );

/**
  Puts the given item into the list at the specified position.  If there
  is already an item at the given position and the list has it's 
  "freeItem" function defined, then it will be used to free said item.
  If no 'freeItem' callback is defined, then the displaced item will
  be returned;
  @param list The list
  @param item The item to put into the list
  @param position The position to place the item in
  @return NULL in successfully inserting the new item and freeing
  any displaced items.  Returns the displaced item if no "freeItem"
  callback is defined.
	*/
void* osrfBigListSet( osrfBigList* list, void* item, unsigned long position );

/**
  Returns the item at the given position
  @param list The list
  @param postiont the position
  */
void* osrfBigListGetIndex( osrfBigList* list, unsigned long  position );

/**
  Frees the list and all list items (if the list has a "freeItem" function defined )
  @param list The list
  */
void osrfBigListFree( osrfBigList* list );

/**
  Removes the list item at the given index
  @param list The list
  @param position The position of the item to remove
  @return A pointer to the item removed if "freeItem" is not defined
  for this list, returns NULL if it is.
  */
void* osrfBigListRemove( osrfBigList* list, int position );

/**
  Finds the list item whose void* is the same as the one passed in
  @param list The list
  @param addr The pointer connected to the list item we're to find
  @return the index of the item, or -1 if the item was not found
  */
int osrfBigListFind( osrfBigList* list, void* addr );


void __osrfBigListSetSize( osrfBigList* list );


/**
  @return The number of non-null items in the list
  */
unsigned long osrfBigListGetCount( osrfBigList* list );

/**
 * May be used as a default memory freeing call
 * Just calls free() on list items
 */
void osrfBigListVanillaFree( void* item );

/**
 * Tells the list to just call 'free()' on each item when
 * an item or the whole list is destroyed
 */
void osrfBigListSetDefaultFree( osrfBigList* list );

#ifdef __cplusplus
}
#endif

#endif
