#!/bin/bash
valgrind --tool=memcheck --leak-check=full --show-leak-kinds=definite --track-origins=yes \
  sqlite3 :memory: '.load dist/tg0' 'select tg_point(1,2);' '.exit 0'
