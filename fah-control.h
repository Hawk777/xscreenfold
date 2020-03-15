#if !defined(FAH_CONTROL_H)
#define FAH_CONTROL_H

#include "poller.h"
#include <stdbool.h>

/**
 * @brief An object that can turn Folding@Home slots on and off.
 */
typedef struct fah_control *fah_control_t;

fah_control_t fah_control_new(poller_t poller);
void fah_control_delete(fah_control_t fah);
bool fah_control_slot_add(fah_control_t fah, unsigned int slot);
bool fah_control_send(fah_control_t fah, bool run);

#endif
