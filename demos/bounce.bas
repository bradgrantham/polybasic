REM GRIFFIN BASIC
10 REM BOUNCING BALLS: SPRITES RUBBED OUT AND REDRAWN EACH FRAME.
20 REM ANY KEY ENDS IT.
30 balls = 6
40 DIM x(balls), y(balls), dx(balls), dy(balls)
50 CLS 0
60 FOR i = 1 TO balls
70 FRECT 0, 0, 16, 16, 0
80 READ red, green, blue
90 FCIRCLE 7, 7, 7, RGB(red, green, blue)
100 FCIRCLE 5, 4, 2, RGB(255, 255, 255)
110 SPRITE DEFINE i, 0, 0, 16, 16
120 x(i) = 20 + i * 40: y(i) = 30 + i * 25
130 dx(i) = 2 + (i - INT(i / 3) * 3): dy(i) = 1 + (i - INT(i / 2) * 2) * 2
140 NEXT i
150 DATA 255,0,0, 0,255,0, 80,80,255, 255,255,0, 255,0,255, 0,255,255
160 wide = SCRW - 16: high = SCRH - 16
170 back = RGB(0, 0, 80)
180 CLS back
190 GPRINT 8, 2, "GRIFFIN BASIC  -  ANY KEY ENDS", RGB(160, 160, 255)
200 REM ONE FRAME: WAIT FOR THE BEAM, THEN MOVE EACH BALL
210 VSYNC
220 FOR i = 1 TO balls
230 FRECT x(i), y(i), 16, 16, back
240 x(i) = x(i) + dx(i): y(i) = y(i) + dy(i)
250 IF x(i) < 0 OR x(i) > wide THEN dx(i) = -dx(i): x(i) = x(i) + dx(i): SOUND 200 + i * 80, 20, 8, 1
260 IF y(i) < 12 OR y(i) > high THEN dy(i) = -dy(i): y(i) = y(i) + dy(i): SOUND 600 + i * 80, 20, 8, 2
270 NEXT i
280 FOR i = 1 TO balls: SPRITE i, x(i), y(i): NEXT i
290 IF INKEY$ = "" THEN 210
300 SOUND OFF
310 TEXT
