#include <string>
#include <vector>
#include <sstream>
#include <unordered_map>

#include <stdio.h>
#include <linenoise.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
//#include <string.h>
#include <errno.h>
#include <libexplain/ptrace.h>
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

void continueExecution(pid_t id){
	ptrace(PTRACE_CONT, id, nullptr, nullptr);
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

user_regs_struct getRegisters(pid_t id){
	user_regs_struct registers;
	ptrace(PTRACE_GETREGS,id,nullptr,&registers);
	return registers;
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

void debug(char name[], pid_t debugeeID){
	wait(debugeeID);
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
			}else if (is_prefix(command, "regs")){
				printRegisters(debugeeID);
			} else{
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
