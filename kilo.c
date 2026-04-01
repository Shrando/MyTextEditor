#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

//0x1f (or 1f) is hex, converted to decimal is 31 and converted to binary is 11111 
//We & the key against 0x1f to basically strip all numbers outside of the last 5 (hence the 11111)
#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/

struct termios orig_termios;

/*** termina ***/

void die(const char *s){
    //Clear screen and move cursor to start
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    //perror prints the error number and message
    perror(s);
    //exit exits the program and returns 1 (anything other than 0 means an error occured)
    exit(1);
}

void disableRawMode(){
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1){
        die("tcsetattr");
    }
}

void enableRawMode(){
    if(tcgetattr(STDIN_FILENO, &orig_termios) == -1){
        die("tcgetattr");
    }
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    //we are getting the binary values for flags (e.g. ECHO is 0001000) and then notting it and anding against the flags
    //This basically keepy every bit from the flags the same, except for the inverted bit inputted, which makes it 0 (e.g. ECHO is now 1110111)

    //IXON ICRNL - Disables some hotkeys like ctrl + c - stops the terminal just closing/stop taking input when these are pressed
    //iflag is input flags (how it handles the user input)
    raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);

    //Stops putting stuff at the end of input (like /n) - Basically cleaner simpler inputs
    //oflag is output flags - it is what happens after the user input has been handled and it is sending stuff to the terminal
    raw.c_oflag &= ~(OPOST);

    //We use OR because we want all bits to become whatever CS8 is (I believe it's just 11111111)
    //CS8 - sets character size to 8 bits per byte - idk what that means, may have to look into it
    raw.c_cflag |= (CS8);

    //Echo - display text inputted (basically hides user input)
    //ICANNON - Read inputs byte by byte (instantly) instead of line by line (after pressing enter)
    //ISIG IEXTEN - turn off more hotkeys
    //lflag is local flag - basically miscellaneous flags
    raw.c_lflag &= ~(ECHO | IEXTEN | ICANON | ISIG);


    //VMIN - sets the minimum amount of bytes needed to for read to return
    raw.c_cc[VMIN]= 0;

    //VTIME - after the set time (in 10ths of a second) if read gets no input it will return 
    raw.c_cc[VTIME] = 1;

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1){
        die("tcsetattr");
    }
}

char editorReadKey(){
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if(nread == -1 && errno != EAGAIN){
            die("read");
        }
    }
    return c;
}

/** input ***/

void editorProcessKeypress(){
    char c = editorReadKey();

    switch(c){
        case CTRL_KEY('q'):
            //Clear screen and move cursor to start
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}

/*** output ***/

void editorDrawRows(){
    for(int y = 0; y < 24; y++){
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}

void editorRefreshScreen(){
    //The 4 is how many bytes we are writing (to the terminal)
    //Bytes: \x1b, [, 2, J
    //'\x1b' is an escape character, allowing an escape sequence, starting with '['
    //'2' is the point for the sequence, meaning entire screen. 1 would be from start to cursor, 0 would be from cursor to end
    //'J' means to erase the display up until the parameter passed in before it (in this case '2')
    write(STDOUT_FILENO, "\x1b[2J", 4);
    //'\xb[' is same as before, it's the start of the escape sequence
    //'H' is to repoisition the cursor 
    //You can choose the position by doing 'x;y' but we want the pointer to be at the start, which it does by default (1;1)
    write(STDOUT_FILENO, "\x1b[H", 3);

    editorDrawRows();

    write(STDOUT_FILENO, "\x1b[H", 3);
}

/*** init ***/

int main() {
    enableRawMode();
    while (1){
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
