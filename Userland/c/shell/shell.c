#include "shell/shell.h"
#include "lib/userlib.h"

// Bucle principal de la shell de usuario
int main(void){
    shellPrintString(WELCOME);
    shellNewline();
    shellPrintString("Escriba 'help' para listar comandos.\n");

    char buff[BUFF_LENGTH];
    while(1){
        shellPrintString("> ");
        sys_set_cursor(1);
        shellReadLine(buff, BUFF_LENGTH);
        sys_set_cursor(0);
        shellNewline();
        processLine(buff, 0);
    }

    return 0;
}

/* Lee una línea del teclado. El cursor lo dibuja el kernel. */
void shellReadLine(char * buffer, uint64_t max){
    char c;
    uint32_t idx = 0;

    while(1){
        if(sys_read(STDIN, &c, 1) != 1){
            continue;
        }

        if(c == '\n'){
            break;
        }

        if(c == '\b'){
            if(idx > 0){
                idx--;
                shellPutchar('\b', STDOUT);
            }
        } else if(c == '+'){
            sys_increase_fontsize();
            redrawFont();
        } else if(c == '-'){
            sys_decrease_fontsize();
            redrawFont();
        } else {
            if(idx + 1 < max){ // lugar para el '\0'
                buffer[idx++] = c;
                shellPutchar(c, STDOUT);
            }
        }
    }

    buffer[idx] = 0;
}

void shellPrintString(char *str){
    if(str == 0){
        return;
    }
    for(uint32_t i = 0; str[i] != '\0'; i++){
        shellPutchar(str[i], STDOUT);
    }
}

// Guarda el caracter para redibujar y lo escribe en fd.
void shellPutchar(char c, uint64_t fd){
    redraw_append_char(c, fd);
    sys_write(fd, &c, 1);
}

void shellNewline(){
    shellPutchar('\n', STDOUT);
}