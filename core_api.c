/* 046267 Computer Architecture - Spring 2020 - HW #4 */

#include "core_api.h"
#include "sim_api.h"

#include <stdio.h>

int num_of_threads = 0;
int switch_penalty = 0;

typedef struct threads
{
	int busyCycles;
	int currLine;
	uint32_t currAdd;
	bool finished;
	tcontext context;
} thread;

typedef struct process
{
	int numOfOps;
	int numOfCycles;
	thread **RR;
} proc;
proc blocked;
proc finegrained;

bool processFinished(thread **RR)
{
	for (int i = 0; i < num_of_threads; i++)
	{
		if (!RR[i]->finished)
		{
			return false;
		}
	}
	return true;
}

void lowerBusyCycles(thread **RR, int cycles)
{
	for (int i = 0; i < num_of_threads; i++)
	{
		RR[i]->busyCycles = RR[i]->busyCycles - cycles;
		if (RR[i]->busyCycles < 0)
		{
			RR[i]->busyCycles = 0;
		}
	}
}

int getNextReadyThread(thread **RR, int current)
{
	int next = -1;
	for (int i = (current + 1) % num_of_threads; i != current; i = (i + 1) % num_of_threads)
	{
		if (RR[i]->busyCycles == 0 && !RR[i]->finished)
		{
			next = i;
			break;
		}
	}
	return next;
}
int getNextUnfinishedThread(thread **RR, int current)
{
	int next = -1;
	for (int i = (current + 1) % num_of_threads; i != current; i = (i + 1) % num_of_threads)
	{
		if (!RR[i]->finished)
		{
			next = i;
			break;
		}
	}
	return next;
}

void doOpcode(thread *th, Instruction *inst)
{
	int src1 = th->context.reg[inst->src1_index];
	int src2 = inst->src2_index_imm;
	if (!inst->isSrc2Imm)
	{
		src2 = th->context.reg[inst->src2_index_imm];
	}
	switch (inst->opcode)
	{
	case CMD_ADD:
		th->context.reg[inst->dst_index] = src1 + src2;
		break;
	case CMD_SUB:
		th->context.reg[inst->dst_index] = src1 - src2;
		break;
	case CMD_ADDI:
		th->context.reg[inst->dst_index] = src1 + src2;
		break;
	case CMD_SUBI:
		th->context.reg[inst->dst_index] = src1 - src2;
		break;
	case CMD_LOAD:
		SIM_MemDataRead(src1 + src2, &th->context.reg[inst->dst_index]);
		th->busyCycles = SIM_GetLoadLat();
		break;
	case CMD_STORE:
		SIM_MemDataWrite(th->context.reg[inst->dst_index] + src2, src1);
		th->busyCycles = SIM_GetStoreLat();
		break;
	case CMD_HALT:
		th->finished = true;
		break;
	default:
		finegrained.numOfOps--;
		break;
	}
}

void CORE_BlockedMT()
{
	// init param
	blocked.numOfOps = 0;
	blocked.numOfCycles = 0;
	Instruction *inst = malloc(sizeof(Instruction));
	

	num_of_threads = SIM_GetThreadsNum();
	switch_penalty = SIM_GetSwitchCycles();
	blocked.RR = malloc(sizeof(thread *) * num_of_threads);
	for (int i = 0; i < num_of_threads; i++)
	{
		blocked.RR[i] = malloc(sizeof(thread));
		blocked.RR[i]->busyCycles = 0;
		blocked.RR[i]->currLine = 0;
		blocked.RR[i]->currAdd = 0;
		blocked.RR[i]->finished = false;
		for (int j = 0; j < REGS_COUNT; j++)
		{
			blocked.RR[i]->context.reg[j] = 0;
		}
	}
	// get next opcode from thread if isn't busy and hasn't finished
	int current = 0;
	bool finished = false;
	while (!finished)
	{
		blocked.numOfOps++;
		blocked.numOfCycles++;
		lowerBusyCycles(blocked.RR, 1);

		SIM_MemInstRead(blocked.RR[current]->currLine, inst, current);
		doOpcode(blocked.RR[current], inst);

		// next line is 4 bytes after
		blocked.RR[current]->currLine += 1;
		blocked.RR[current]->currAdd += 4;

		// if this thread is finished
		if (blocked.RR[current]->finished)
		{
			if (getNextUnfinishedThread(blocked.RR, current) == -1)
			{
				finished = true;
				return;
			}
			else
			{
				//get next thread
				current = getNextReadyThread(blocked.RR, current);
				while (current == -1)
				{
					//if next thread is busy, wait on current thread until available
					blocked.numOfCycles += 1;
					lowerBusyCycles(blocked.RR, 1);
					current = getNextReadyThread(blocked.RR, current);
				}
			}
			// context switch
			blocked.numOfCycles += switch_penalty;
			lowerBusyCycles(blocked.RR, switch_penalty);
		}

		while (blocked.RR[current]->busyCycles > 0 && !blocked.RR[current]->finished)
		{
			int next = getNextReadyThread(blocked.RR, current);
			// if there is another thread ready, switch
			if (next != -1)
			{
				// context switch
				blocked.numOfCycles += switch_penalty;
				lowerBusyCycles(blocked.RR, switch_penalty);
				current = next;
				break;
			}
			// if there are no ready threads
			else
			{
				blocked.numOfCycles++;
				// lower busycycles for all threads
				lowerBusyCycles(blocked.RR, 1);
			}
		}
	}
}
void CORE_FinegrainedMT()
{
	// init param
	finegrained.numOfOps = 0;
	finegrained.numOfCycles = 0;
	Instruction *inst = malloc(sizeof(Instruction));
	num_of_threads = SIM_GetThreadsNum();
	finegrained.RR = malloc(sizeof(thread*) * num_of_threads);
	for (int i = 0; i < num_of_threads; i++)
	{
		finegrained.RR[i] = malloc(sizeof(thread));
		finegrained.RR[i]->busyCycles = 0;
		finegrained.RR[i]->currLine = 0;
		finegrained.RR[i]->currAdd = 0;
		finegrained.RR[i]->finished = false;
		for (int j = 0; j < REGS_COUNT; j++)
		{
			finegrained.RR[i]->context.reg[j] = 0;
		}
	}

	int current = 0;
	int next = 0;
	bool finished = false;
	while (!processFinished(finegrained.RR))
	{
		finegrained.numOfOps++;
		finegrained.numOfCycles++;
		lowerBusyCycles(finegrained.RR, 1);
		// get next opcode from thread
		SIM_MemInstRead(finegrained.RR[current]->currLine, inst, current);
		doOpcode(finegrained.RR[current], inst);

		// next line is 4 bytes after
		finegrained.RR[current]->currLine += 1;
		finegrained.RR[current]->currAdd += 4;

		next = getNextReadyThread(finegrained.RR, current);

		
			// while no other thread is ready wait here
			while (next == -1 && finegrained.RR[current]->busyCycles >0 )
			{
				
				// if there is another thread ready, switch
				if (next != -1)
				{
					current = next;
					break;
				}
				// if there are no ready threads
				else
				{
					finegrained.numOfCycles++;
					// lower busycycles for all threads
					lowerBusyCycles(finegrained.RR, 1);
				}
				next = getNextReadyThread(finegrained.RR, current);
			}
			
		if (next != -1)
		{
			current=next;
		}
		
	}
}
double CORE_BlockedMT_CPI()
{
	// free?
	return (double)blocked.numOfCycles / (double)blocked.numOfOps;
}

double CORE_FinegrainedMT_CPI()
{
	// free?
	return (double)finegrained.numOfCycles / (double)finegrained.numOfOps;
}

void CORE_BlockedMT_CTX(tcontext *context, int threadid)
{
	for (int i = 0; i < REGS_COUNT; i++)
	{
		context[threadid].reg[i] = blocked.RR[threadid]->context.reg[i];
	}
}

void CORE_FinegrainedMT_CTX(tcontext *context, int threadid)
{
	for (int i = 0; i < REGS_COUNT; i++)
	{
		context[threadid].reg[i] = finegrained.RR[threadid]->context.reg[i];
	}
}