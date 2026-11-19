REM GRIFFIN BASIC
10 REM A ROSETTE: EVERY POINT ON A CIRCLE JOINED TO EVERY OTHER
20 points = 19
30 DIM px(points), py(points)
40 CLS 0
50 cx = SCRW / 2: cy = SCRH / 2 + 4: radius = 108
60 FOR i = 1 TO points
70 angle = i * 6.283185 / points
80 px(i) = INT(cx + radius * SIN(angle) + .5)
90 py(i) = INT(cy - radius * COS(angle) + .5)
100 NEXT i
110 FOR i = 1 TO points - 1
120 FOR j = i + 1 TO points
130 LINE px(i), py(i), px(j), py(j), RGB(i * 13, 255 - j * 12, 128 + (j - i) * 6)
140 NEXT j
150 NEXT i
160 GPRINT 4, 2, "ROSETTE", RGB(255, 255, 255)
