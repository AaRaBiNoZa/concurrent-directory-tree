#ifndef MIMUW_FORK__SYNCHRO_H_
#define MIMUW_FORK__SYNCHRO_H_
#include <pthread.h>
#include <stdbool.h>

/**
 * Structure to synchronize access to a node.
 * Many nodes may read (access) data at once but only one can modify.
 */
struct Synchro {
  // next seven variables are equivalent to variables needed
  // in reader/writer problem solved with monitors on lecture
  pthread_mutex_t lock;
  pthread_cond_t can_modify;
  pthread_cond_t can_access;
  int accessing_count;
  int accessing_waiting;
  int modifying_waiting;
  bool is_modifying;

  // this conditional variable ensures that if a thread signals it's
  // desire to remove the node, it's flagged and after all awaiting operations
  // get done, the thread may proceed with deletion
  pthread_cond_t can_be_removed;

  // this variable helps to ensure that readers' cascading waking up
  // doesn't end up starving "writers"
  int how_many_to_wake;

  // to simulate the behaviour of conditional variables on the lecture
  bool modify_now;

  // flag, so we know not to grant access any new threads
  bool want_to_be_removed;
};

/**
 * Initializes variables in the Synchro structure
 * @param synchronizer pointer to a Synchro struct that will be initialized
 */
void synchro_init(struct Synchro *synchronizer);

/**
 * Destroys data in the Synchro structure
 * @param synchronizer pointer to a Synchro struct to be destroyed
 */
void synchro_destroy(struct Synchro *synchronizer);

/**
 * Similar to a reader's entry protocol.
 * Let's a thread "into" the node. Grants reading rights. If a thread
 * passes this function then it has reading rights to data in the node.
 * Multiple threads can have reading rights at once.
 * @param synchronizer pointer to a Synchro struct belonging to a particular
 * node
 */
void synchro_visit(struct Synchro *synchronizer);

/**
 * Similar to a reader's exit protocol.
 * A thread that is leaving the node and surrendering it's reading rights calls
 * this function.
 * @param synchronizer
 */
void synchro_leave_after_visiting(struct Synchro *synchronizer);

/**
 * Similar to a writers entry protocol.
 * Let's a thread "into" the node. Grants modifying(writing) rights. If a thread
 * passes this function then it has modifying rights to data in the node.
 * Only one thread can have modifying rights at once.
 * @param synchronizer
 */
void synchro_modify(struct Synchro *synchronizer);

/**
 * Similar to a writer's exit protocol.
 * A thread that is leaving the node and surrendering it's modifying rights
 * calls this function.
 * @param synchronizer
 */
void synchro_leave_after_modifying(struct Synchro *synchronizer);

/**
 * A function that marks the current reader as a waiting writer.
 * Basically equivalent to calling this function
 * synchro_leave_after_visiting(s)
 * synchro_modify(s)
 * but limits possible mutex locks/unlocks that are unnecessary
 * @param synchronizer
 */
void synchro_change_from_visiting_to_mod(struct Synchro *synchronizer);

/**
 * Flags a node to be removed, doesn't let anyone new get in queue for access.
 * @param synchronizer
 */
void synchro_prepare_for_being_removed(struct Synchro *synchronizer);

/**
 * If for some reason a node cannot be removed, this function is called.
 * It "lifts" the "to be removed" flag and leaves the node in  a valid state.
 * @param synchronizer
 */
void synchro_leave_after_bad_remove(struct Synchro *synchronizer);

#endif // MIMUW_FORK__SYNCHRO_H_
