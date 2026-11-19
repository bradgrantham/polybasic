BASIC for Griffin Jr.

  D:                     switch to the card
  basic                  direct mode; QUIT returns to the monitor
  basic STARTREK.BAS     run a program
  basic -m               direct mode in Microsoft BASIC (see below)
  basic -s               use serial port A (38400 8N1) as the console

In direct mode: LOAD "LUNAR.BAS", RUN, LIST, NEW, SAVE "NAME.BAS".
Ctrl-C stops a running program.

BASIC here is Griffin BASIC: long names, graphics and sound, and
keywords that must stand alone (GOTO 100, not GOTO100). A file whose
first line is REM MICROSOFT BASIC runs as Microsoft BASIC, where
GOTO100 works; so does anything under basic -m.

Programs (Microsoft BASIC)

  STARTREK.BAS   Super Star Trek (1978). Wider than the screen in places.
  LUNAR.BAS      Lunar lander. Wider than the screen in places.
  LIFE.BAS       Conway's Life: type a pattern, then DONE.
  3DPLOT.BAS     A surface plotted in characters. Wider than the screen.
  SINE.BAS       Sine Wave as published. Wider than the screen.
  SINEJR.BAS     Sine Wave narrowed to 53 columns.
  MANDEL.BAS     The Mandelbrot set in characters. Takes about a minute.
  BIORHYTHM.BAS  Biorhythm chart from a birth date.
  ADVENTURE.BAS  Griffin's Keep, a small adventure. Type HELP.
  FIZZBUZZ.BAS   The usual.

Griffin BASIC programs (graphics and sound; a picture stays up until
a key is pressed)

  BOUNCE.BAS     Bouncing sprites on two pages, with sound. Any key ends.
  ROSETTE.BAS    A figure of coloured lines.
  MUSIC.BAS      Ode to Joy on three voices. Any key stops it.

The first five are from "BASIC Computer Games" (Creative Computing) and
were written for terminals of 64 or more columns; this screen has 53.
