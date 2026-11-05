#include <stdint.h>
#include "lib/userlib.h"
#include "shell/shell.h"
#include "commands/commands.h"

// kill <pid> [pid ...]: mata los procesos indicados.
void kill_cmd(void){
    const char *a = cmd_args();
    if(a == 0){
        shellPrintString("uso: kill <pid> [pid ...]\n");
        return;
    }
    int enviados = 0;
    int64_t pid;
    while((pid = next_uint(&a)) > 0){
        if(sys_kill((uint64_t)pid) < 0)
            shellPrintString("kill: PID inexistente o de un proceso del sistema\n");
        enviados++;
    }
    if(enviados == 0)
        shellPrintString("uso: kill <pid> [pid ...]\n");
}
