#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

//0x1f (or 1f) is hex, converted to decimal is 31 and converted to binary is 11111 
//We & the key against 0x1f to basically strip all numbers outside of the last 5 (hence the 11111)
#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/
struct editorConfig{
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;

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
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1){
        die("tcsetattr");
    }
}

void enableRawMode(){
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1){
        die("tcgetattr");
    }
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
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

int getCursorPosition(int *rows, int *cols){
    //create buffer
    char buf[32];
    unsigned int i = 0;

    //Get cursor position
    if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    //goes through each index of the buffer (each index being a character/byte)
    //reads the current terminal and breaks if errors occurs. each buf index is passed in as a reference.
    while(i < sizeof(buf) - 1){
        if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if(buf[i] == 'R') break;
        i++;
    }
    //ends buf with \0 because we're gonna be using buf as a string and strings need to end in \0 for printf
    buf[i] = '\0';

    //Check the start of the buffer to make sure it has escape sequence \x1b and [
    if(buf[0] != '\x1b' || buf[1] != '[') return -1;
    //Then we do an sscanf of the characters right after
    //We pass in string %d;%d and then rows and cols, essentially telling it to parse the numbers found in format num;num to parse into rows and cols
    if(sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols){
    //Gets the size of the widnow and assigns it to ws_col and ws_row in ws. If failed, returns -1 or ws_cols will be 0.
    struct winsize ws;

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        //This is a backup attempt for if the default winsize ioctl doesn't work
        //Move cursor 999 right and 999 down (stops at edge of screen)
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows,cols);
    }
    else{
        *cols= ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
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

/*** append buffer ***/
//This will be used to create a write buffer instead of individual writes. This'll prevent flickering/weird visual delay between writes by buffering them all together.
struct abuf{
    char *b;
    int len;
}

//This is basically a definition for creating a buffer with NULL characters and 0 len (essentially an empty buffer)
#define ABUF_INIT {NULL, 0}

/*** output ***/

void editorDrawRows(){
    for(int y = 0; y < E.screenrows; y++){
        //print ~ in every line
        write(STDOUT_FILENO, "~", 3);
        //print \r\n (basically new line) on every line except last
        if(y < E.screenrows - 1){
        write(STDOUT_FILENO, "\r\n", 3);
        }
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

void initEditor(){
    //Called on initialisation, assigns the correct values in our E (editorConfig struct) and if fails (which means -1 returned) it will die
    if(getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();

    while (1){
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
