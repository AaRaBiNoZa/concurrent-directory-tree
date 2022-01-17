#include "Synchro.h"
#include "err.h"

void synchro_init(struct Synchro *synchronizer) {
  int err;
  if ((err = pthread_mutex_init(&(synchronizer->lock), 0)) != 0) {
    syserr(err, "mutex_init failed");
  }
  if ((err = pthread_cond_init(&(synchronizer->can_modify), 0) != 0)) {
    syserr(err, "cond_init failed");
  }
  if ((err = pthread_cond_init(&(synchronizer->can_access), 0) != 0)) {
    syserr(err, "cond_init failed");
  }
  if ((err = pthread_cond_init(&(synchronizer->can_be_removed), 0) != 0)) {
    syserr(err, "cond_init failed");
  }
  synchronizer->is_modifying = false;
  synchronizer->modify_now = false;
  synchronizer->accessing_count = 0;
  synchronizer->accessing_waiting = 0;
  synchronizer->modifying_waiting = 0;
  synchronizer->how_many_to_wake = 0;
  synchronizer->want_to_be_removed = false;
}

void synchro_destroy(struct Synchro *synchronizer) {
  int err;
  if ((err = pthread_mutex_destroy(&(synchronizer->lock))) != 0) {
    syserr(err, "mutex_destroy failed");
  }
  if ((err = pthread_cond_destroy(&(synchronizer->can_modify)) != 0)) {
    syserr(err, "cond_destroy failed");
  }
  if ((err = pthread_cond_destroy(&(synchronizer->can_access)) != 0)) {
    syserr(err, "cond_destroy failed");
  }
  if ((err = pthread_cond_destroy(&(synchronizer->can_be_removed)) != 0)) {
    syserr(err, "cond_destroy failed");
  }
}

void synchro_visit(struct Synchro *synchronizer) {
  int err;
  if ((err = pthread_mutex_lock(&(synchronizer->lock))) != 0) {
    syserr(err, "mutex_lock failed");
  }

  while (synchronizer->is_modifying || synchronizer->modify_now ||
         synchronizer->modifying_waiting) {

    synchronizer->accessing_waiting++;

    if ((err = pthread_cond_wait(&(synchronizer->can_access),
                                 &(synchronizer->lock))) != 0) {
      syserr(err, "cond_wait failed");
    }
    synchronizer->accessing_waiting--;

    // imitating inheritance of critical section
    if (synchronizer->how_many_to_wake > 0) {
      synchronizer->how_many_to_wake--;
      break;
    }
  }

  synchronizer->accessing_count++;

  if ((err = pthread_mutex_unlock(&(synchronizer->lock))) != 0) {
    syserr(err, "mutex_unlock failed");
  }
}

void synchro_leave_after_visiting(struct Synchro *synchronizer) {
  int err;
  if ((err = pthread_mutex_lock(&(synchronizer->lock))) != 0) {
    syserr(err, "mutex_lock failed");
  }
  synchronizer->accessing_count--;

  if (synchronizer->accessing_count == 0 &&
      synchronizer->how_many_to_wake == 0 &&
      synchronizer->modifying_waiting > 0) {
    synchronizer->modify_now = true;
    if ((err = pthread_cond_broadcast(&(synchronizer->can_modify))) != 0) {
      syserr(err, "cond_broadcast failed");
    }
  } else if (synchronizer->accessing_count == 0 &&
             synchronizer->accessing_waiting == 0 &&
             synchronizer->modifying_waiting == 0 &&
             synchronizer->want_to_be_removed) {
    if ((err = pthread_cond_broadcast(&(synchronizer->can_be_removed))) != 0) {
      syserr(err, "cond_broadcast failed");
    }
  }
  if ((err = pthread_mutex_unlock(&(synchronizer->lock))) != 0) {
    syserr(err, "mutex_unlock failed");
  }
}

/**
 * extracted body of synchro_modify, to use in both synchro_modify and
 * synchro_change_from_visiting_to_mod
 */
void synchro_modify_while_holding_mutex(struct Synchro *synchronizer) {
  int err;

  while (!synchronizer->modify_now &&
         (synchronizer->accessing_count > 0 || synchronizer->is_modifying ||
          synchronizer->how_many_to_wake > 0)) {

    synchronizer->modifying_waiting++;
    if ((err = pthread_cond_wait(&(synchronizer->can_modify),
                                 &(synchronizer->lock))) != 0) {
      syserr(err, "cond_wait failed");
    }
    synchronizer->modifying_waiting--;
  }

  synchronizer->modify_now = false;
  synchronizer->is_modifying = true;
}

void synchro_change_from_visiting_to_mod(struct Synchro *synchronizer) {
  int err;
  if ((err = pthread_mutex_lock(&(synchronizer->lock))) != 0) {
    syserr(err, "mutex_lock failed");
  }

  synchronizer->accessing_count--;
  synchro_modify_while_holding_mutex(synchronizer);

  if ((err = pthread_mutex_unlock(&(synchronizer->lock))) != 0) {
    syserr(err, "mutex_unlock failed");
  }
}

void synchro_modify(struct Synchro *synchronizer) {
  int err;
  if ((err = pthread_mutex_lock(&(synchronizer->lock))) != 0) {
    syserr(err, "mutex_lock failed");
  }

  synchro_modify_while_holding_mutex(synchronizer);

  if ((err = pthread_mutex_unlock(&(synchronizer->lock))) != 0) {
    syserr(err, "mutex_unlock failed");
  }
}

void synchro_leave_after_modifying(struct Synchro *synchronizer) {
  int err;
  if ((err = pthread_mutex_lock(&(synchronizer->lock))) != 0) {
    syserr(err, "mutex_lock failed");
  }

  synchronizer->is_modifying = false;
  if (synchronizer->accessing_waiting > 0) {
    synchronizer->how_many_to_wake = synchronizer->accessing_waiting;
    if ((err = pthread_cond_broadcast(&(synchronizer->can_access))) != 0) {
      syserr(err, "cond_broadcast failed");
    }
  } else if (synchronizer->modifying_waiting > 0) {
    synchronizer->modify_now = true;
    if ((err = pthread_cond_broadcast(&(synchronizer->can_modify))) != 0) {
      syserr(err, "cond_broadcast failed");
    }
  } else if (synchronizer->accessing_waiting == 0 &&
             synchronizer->modifying_waiting == 0 &&
             synchronizer->want_to_be_removed) {
    if ((err = pthread_cond_broadcast(&(synchronizer->can_be_removed))) != 0) {
      syserr(err, "cond_broadcast failed");
    }
  }
  if ((err = pthread_mutex_unlock(&(synchronizer->lock))) != 0) {
    syserr(err, "mutex_unlock failed");
  }
}

void synchro_prepare_for_being_removed(struct Synchro *synchronizer) {
  int err;
  if ((err = pthread_mutex_lock(&(synchronizer->lock))) != 0) {
    syserr(err, "mutex_lock failed");
  }

  synchronizer->want_to_be_removed = true;
  while (synchronizer->accessing_count != 0 || synchronizer->is_modifying ||
         synchronizer->modifying_waiting != 0 ||
         synchronizer->accessing_waiting != 0) {

    if ((err = pthread_cond_wait(&(synchronizer->can_be_removed),
                                 &(synchronizer->lock))) != 0) {
      syserr(err, "cond_wait failed");
    }
  }

  if ((err = pthread_mutex_unlock(&(synchronizer->lock))) != 0) {
    syserr(err, "mutex_unlock failed");
  }
}

void synchro_leave_after_bad_remove(struct Synchro *synchronizer) {
  int err;
  if ((err = pthread_mutex_lock(&(synchronizer->lock))) != 0) {
    syserr(err, "mutex_lock failed");
  }
  synchronizer->want_to_be_removed = false;

  if ((err = pthread_mutex_unlock(&(synchronizer->lock))) != 0) {
    syserr(err, "mutex_unlock failed");
  }
}
