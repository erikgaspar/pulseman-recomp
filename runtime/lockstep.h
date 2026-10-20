/* Verificação lockstep: compara cada bloco recompilado com o Musashi. */
#ifndef PM_LOCKSTEP_H
#define PM_LOCKSTEP_H

#include <stdint.h>

extern int musashi_replay;
extern int lockstep_recording;

void lockstep_record(int is_write, uint32_t addr, int size, uint32_t value);
uint32_t lockstep_replay_read(uint32_t addr, int size);
void lockstep_replay_write(uint32_t addr, int size, uint32_t value);
void lockstep_report(void);

#endif
