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
// Code for the int 3 instruction in x86 that stops execution of debugee process.
#define interrupt 0xCC

struct breakpoint{
	pid_t pid;
	void *address;
	bool enabled;
	uint8_t overwrittenByte;
};
std::unordered_map<void *,breakpoint> breakpoints;

void disableBreakpoint(void * address){
	auto b = breakpoints[address];
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
	disableBreakpoint(breakpoint.address);
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

//void toFile(char *string){
//	FILE * file = fopen("output","w");
//	fprintf(file,"%s",string);
//	fclose(file);
//}

void continueExecution(pid_t debugeeID){
	int wait_status;
    auto options = 0;
	ptrace(PTRACE_CONT, debugeeID, nullptr, nullptr);
    waitpid(debugeeID, &wait_status, options);
}

void debug(char name[], pid_t debugeeID){
	int wait_status;
    auto options = 0;
    waitpid(debugeeID, &wait_status, options);
	printf("got here \n");
    char* line = nullptr;
    while((line = linenoise("<debugger> ")) != nullptr) {
//		toFile(line);
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
				int counter;
				while(ptrace(PTRACE_SINGLESTEP,debugeeID,0,0) != -1 ){
    				waitpid(debugeeID, &wait_status, options);
					//printf("%i \n",ptrace(PTRACE_SINGLESTEP,debugeeID,0,0));
					counter++;
				}
				printf("%i \n",counter);
//waitpid(debugeeID,&wait_status,options);
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
			printf("Got here too \n");
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
