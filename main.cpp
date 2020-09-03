#include <string>
#include <vector>
#include <sstream>

#include <stdio.h>
#include <linenoise.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

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

void debug(char name[], pid_t debugeeID){
	int wait_status;
    auto options = 0;
    waitpid(debugeeID, &wait_status, options);

    char* line = nullptr;
    while((line = linenoise("<debugger> ")) != nullptr) {
//		toFile(line);
		if(line[0] != '\0'){
			auto args = split(line,' ');
    		auto command = args[0];
    		if (is_prefix(command, "cont")) {
				ptrace(PTRACE_CONT, debugeeID, nullptr, nullptr);
    			waitpid(debugeeID, &wait_status, options);
    		}
    		else {
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
			execl(programName,programName,NULL);
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
