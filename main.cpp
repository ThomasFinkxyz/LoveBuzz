#include <string>
#include <vector>
#include <sstream>
#include <unordered_map>
#include <map>

#include <stdio.h>
#include <linenoise.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
//#include <string.h>
//#include <errno.h>
//#include <libexplain/ptrace.h>
#include <cassert>
#include <sys/user.h>
// Code for the int 3 instruction in x86 that stops execution of debugee process.
#define interrupt 0xCC

struct breakpoint{
	pid_t pid;
	void *address;
	bool enabled;
	uint8_t overwrittenByte;
};

uint64_t readMemory(pid_t id,uintptr_t addr){
	return ptrace(PTRACE_PEEKDATA, id, addr, nullptr);
}
std::unordered_map<void *,breakpoint> breakpoints;

void disableBreakpoint(void * address){
	auto b = breakpoints[address];
	printf("Disabling breakpoint at %x with pid %i \n",address,b.pid);
	auto word = ptrace(PTRACE_PEEKDATA, b.pid, address, nullptr);
    auto oldWord = ((word & ~0xff) | b.overwrittenByte);
    ptrace(PTRACE_POKEDATA, b.pid, address, oldWord);
	printf("Overwrote %x with %x at address %x \n", word,oldWord,address);

    b.enabled = false;
}


void addBreakpoint(pid_t id, std::intptr_t addr){
	auto breakpoint = breakpoints[(void*)addr];
	if(breakpoint.enabled)
		return;
	breakpoint.pid = id;
	breakpoint.address = (void*)addr;

	const uintptr_t address = addr;
	const uintptr_t int3Opcode = interrupt;
	auto unmodifiedWord = ptrace(PTRACE_PEEKTEXT,id,address,0);
	printf("PTRACE_PEEKTEST: %x from address %x \n",unmodifiedWord,address);

	breakpoint.overwrittenByte = static_cast<uint8_t>(unmodifiedWord & 0xff); //Gets most significant byte on x86

	auto wordWithInt3 = ((unmodifiedWord & ~0xFF) | int3Opcode);
	printf("wordWithInt3: %x \n", wordWithInt3);
	auto result = ptrace(PTRACE_POKETEXT, id, address, wordWithInt3);
	if(result != -1){
		printf("Previous word at address %x: %x and current word %x most significant byte: %x \n",address, unmodifiedWord, wordWithInt3, breakpoint.overwrittenByte);
	}

	breakpoint.enabled = true;
	breakpoints[(void*)addr] = breakpoint;
	//disableBreakpoint((void*)addr);
}

std::vector<std::string> split(const std::string &s, char delimiter) {
    std::vector<std::string> out{};
    std::stringstream ss {s};
    std::string item;

    while (std::getline(ss,item,delimiter)) {
        out.push_back(item);
    }

    return out;
}

bool is_prefix(const std::string& s, const std::string& of) {
    if (s.size() > of.size()) return false;
    return std::equal(s.begin(), s.end(), of.begin());
}

void wait(pid_t id){
	int wait_status;
    auto options = 0;
    waitpid(id, &wait_status, options);
}

void step(pid_t id){
	ptrace(PTRACE_SINGLESTEP,id,0,0);
	wait(id);
}



void test(pid_t id){
	addBreakpoint(id,0x401da4);
	printf("%x \n",readMemory(id,0x401da4));
	//assert(readMemory(id,0x401da4) == (uint64_t)0x8087cc);
	disableBreakpoint((void*)0x401da4);
	//assert(readMemory(id,0x401da4) == (uint64_t)0x8087e8);
	printf("%x \n",readMemory(id,0x401da4));
}

// Tutorial had orig_rax as -1 but since it was the same as the pc
// I changed it. Those registers doesn't seem to have a Dwarf number
// as far as I can tell.

static std::map<std::string,int> regName2Num = {
	{ "r15", 15},
	{"r14", 14},
	{"r13" , 13},
	{"r12" , 12},
	{"rbp" , 6},
	{"rbx" , 3},
	{"r11" , 11},
 	{"r10" , 10},
	{"r9" ,   9},
	{"r8" ,   8},
	{"rax" ,  0},
	{"rcx" , 2},
	{"rdx" ,  1},
	{"rsi" ,  4},
	{"rdi" ,  5},
	{"orig_rax", -2},
	{"rip" ,   -1},
	{"cs" ,    51},
	{"eflags", 49},
	{"rsp" ,   7},
	{"ss" ,    52},
	{"fs_base",58},
	{"gs_base", 59},
	{"ds" ,  53},
	{"es" ,   50},
	{"fs" ,   54},
	{"gs" ,   55}
};

user_regs_struct getRegisters(pid_t id){
	user_regs_struct registers;
	ptrace(PTRACE_GETREGS,id,nullptr,&registers);
	return registers;
}

uint64_t readReg(pid_t id, int dwarfNum){
	user_regs_struct regs = getRegisters(id);
	switch(dwarfNum){
		case 15:
			return regs.r15;
		case 14:
			return regs.r14;
		case 13:
			return regs.r13;
		case 12:
			return regs.r12;
		case 6:
			return regs.rbp;
		case 3:
			return regs.rbx;
		case 11:
			return regs.r11;
		case 10:
			return regs.r10;
		case 9:
			return regs.r9;
		case 8:
			return regs.r8;
		case 0:
			return regs.rax;
		case 2:
			return regs.rcx;
		case 1:
			return regs.rdx;
		case 4:
			return regs.rsi;
		case 5:
			return regs.rdi;
		case -2:
			return regs.orig_rax;
		case -1:
			return regs.rip;
		case 51:
			return regs.cs;
		case 49:
			return regs.eflags;
		case 7:
			return regs.rsp;
		case 52:
			return regs.ss;
		case 58:
			return regs.fs_base;
		case 59:
			return regs.gs_base;
		case 53:
			return regs.ds;
		case 50:
			return regs.es;
		case 54:
			return regs.fs;
		case 55:
			return regs.gs;
		default:
			printf("Error: No register with Dwarf num of %i \n",dwarfNum);
			return 0;
	}

}

uint64_t readReg(pid_t id, std::string name){
	return readReg(id,regName2Num[name]);
}


void printRegisters(pid_t id){
	struct user_regs_struct regs;
	regs = getRegisters(id);
	printf("r15: %x r14: %x r13: %x r12: %x rbp: %x \n"
	"rbx: %x r11: %x r10: %x r9: %x r8: %x \n"
	"rax: %x rcx: %x rdx: %x rsi: %x rdi: %x \n"
	"orig_rax: %x rip: %x cs: %x eflags: %x \n"
	"rsp: %x ss: %x fs_base: %x gs_base: %x \n"
	"ds: %x es: %x fs: %x gs: %x \n",
	regs.r15,regs.r14,regs.r13,regs.r12,regs.rbp,
	regs.rbx,regs.r11,regs.r10,regs.r9,regs.r8,
	regs.rax,regs.rcx,regs.rdx,regs.rsi,regs.rdi,
	regs.orig_rax,regs.rip,regs.cs,regs.eflags,
	regs.rsp,regs.ss,regs.fs_base,regs.gs_base,
	regs.ds,regs.es,regs.fs,regs.gs);
}

// This is ugly, long and almost a copy and paste of the other function. But at least a big switch
// is easy to understand. Can't think of a better way to do it.
void writeReg(pid_t id, int dwarfNum, uint64_t value){
	user_regs_struct regs;
	ptrace(PTRACE_GETREGS,id,nullptr,&regs);
	switch(dwarfNum){
		case 15:
			regs.r15 = value;
			break;
		case 14:
			regs.r14 = value;
			break;
		case 13:
			regs.r13 = value;
			break;
		case 12:
			regs.r12 = value;
			break;
		case 6:
			regs.rbp = value;
			break;
		case 3:
			regs.rbx = value;
			break;
		case 11:
			regs.r11 = value;
			break;
		case 10:
			regs.r10 = value;
			break;
		case 9:
			regs.r9 = value;
			break;
		case 8:
			regs.r8 = value;
			break;
		case 0:
			regs.rax = value;
			break;
		case 2:
			regs.rcx = value;
			break;
		case 1:
			regs.rdx = value;
			break;
		case 4:
			regs.rsi = value;
			break;
		case 5:
			regs.rdi = value;
			break;
		case -2:
			regs.orig_rax = value;
			break;
		case -1:
			regs.rip = value;
			break;
		case 51:
			regs.cs = value;
			break;
		case 49:
			regs.eflags = value;
			break;
		case 7:
			regs.rsp = value;
			break;
		case 52:
			regs.ss = value;
			break;
		case 58:
			regs.fs_base = value;
			break;
		case 59:
			regs.gs_base = value;
			break;
		case 53:
			regs.ds = value;
			break;
		case 50:
			regs.es = value;
			break;
		case 54:
			regs.fs = value;
			break;
		case 55:
			regs.gs = value;
			break;
		default:
			printf("Error: No register with Dwarf num of %i \n",dwarfNum);
	}
	ptrace(PTRACE_SETREGS,id,nullptr,&regs);
}


void writeReg(pid_t id,std::string name,uint64_t value){
	writeReg(id,regName2Num[name], value);
}

void continueExecution(pid_t id){
	uint64_t lastInstructionLoc = readReg(id,"rip")-1;
	if(breakpoints.count((void *)lastInstructionLoc)){
		if(breakpoints[(void *)lastInstructionLoc].enabled){
			writeReg(id,"rip",lastInstructionLoc);
			disableBreakpoint((void *)lastInstructionLoc);
			ptrace(PTRACE_SINGLESTEP,id,NULL,NULL);
			wait(id);
			addBreakpoint(id,(std::intptr_t)lastInstructionLoc);
		}
	}
	ptrace(PTRACE_CONT, id, nullptr, nullptr);
	wait(id);
}

void debug(char name[], pid_t debugeeID){
	wait(debugeeID);
	printf("Debugee process id: %i \n",debugeeID);
    char* line = nullptr;
    while((line = linenoise("<debugger> ")) != nullptr) {
		if(line[0] != '\0'){
			auto args = split(line,' ');
    		auto command = args[0];
    		if (is_prefix(command, "cont")) {
				continueExecution(debugeeID);
    		}
    		else if(is_prefix(command, "break")){
				std::string addr {args[1], 2};
        		addBreakpoint(debugeeID,std::stol(addr, 0, 16));
			}else if(is_prefix(command, "step")){
				step(debugeeID);
			}else if(is_prefix(command, "test")){
				test(debugeeID);
			}else if (is_prefix(command, "dump")){
				printRegisters(debugeeID);
				printf("rip is %x \n",readReg(debugeeID,"rip"));
			} else if (is_prefix(command, "reg")){
					if(args[2] == "read"){
						printf("%x \n",readReg(debugeeID,args[1]));
					} else if(args[2] == "write"){
						writeReg(debugeeID,args[1],std::stoul(args[3],0,16));
					}else{
						printf("Unkown comman \n");
					}
			}else {
    			printf("Unknown command\n");
			}
    		linenoiseHistoryAdd(line);
    		linenoiseFree(line);
		}
	}
}

int main(int argc, char* argv[]){
	if(argc < 2){
		printf("Error: Program name was not passed as an argument.\n");
		return -1;
	}
	auto programName = argv[1];
	auto pid = fork();
	switch(pid){
		case 0:
			//Debugee
			ptrace(PTRACE_TRACEME, 0, NULL,NULL);
			execve(programName,NULL,NULL);
			break;
		case -1:
			printf("Error: Failed to start %s \n",programName);
			return -1;
		default:
			//Debugger
			debug(programName,pid);
			break;
	}
}
