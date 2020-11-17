#include <ncurses.h>

int main()
{
    initscr();
    keypad(stdscr, true);   //Включаем режим чтения функциональных клавиш
    noecho();               //Выключаем отображение вводимых символов, нужно для getch()
    halfdelay(100);         //Устанавливаем ограничение по времени ожидания getch() в 10 сек

    printw("Press F2 to exit.\n");

    bool ex = false;
    while ( !ex )
    {
        int ch = getch();

        switch ( ch )
        {
        case ERR:
            //printw("Please, press any key...\n"); //Если нажатия не было, напоминаем пользователю, что надо нажать клавишу
            break;
        case KEY_F(2): //Выходим из программы, если была нажата F2
            ex = true;
            break;
        default:  //Если всё нормально, выводим код нажатой клавиши
            printw("%d\n", ch);
            break;
        }

        refresh(); //Выводим на настоящий экран

    }

    printw("Thank you. Good buy!");
    getch();
    endwin();
    return 0;
}
