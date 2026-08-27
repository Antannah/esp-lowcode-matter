# -*- coding: utf-8 -*-
"""
Created on Sun May 24 16:37:35 2015

@author: Norman
"""
import numpy as np
import matplotlib.pyplot as plt

# gemessene Temperaturen
#Temp = np.array([ 24.0 ,26.0 ,28.0 , 30.0 , 32.0 , 34.0 , 36.0 , 38.0 , 40.0 ,
#	 45.0 , 50.0 , 55.0 , 60.0 , 65.0 , 70.0 , 75.0 , 80.0 , 85.0 ,
#	 90.0 , 95.0 , 100.0, 105.0, 110.0, 115.0, 120.0, 125.0, 130.0,
#	 135.0, 140.0, 150.0, 160.0, 170.0, 180.0, 190.0, 200.0, 210.0,
#	 220.0, 230.0, 240.0, 250.0, 260.0, 270.0, 280.0])
# gemessene Widerstandswerte
#R = np.array([1050 ,945 ,850,770,695,635,585,535,485,370,295,235,187,151,126,105,
#     87.5,71.5,59.2,49.25,41.5,34.3,29.5,24.9,21.6,16.75,15.75,13.48,11.5,
#     8.7,6.9,5.4,4.05,3.13,2.52,2.06,1.61,1.297,1.062,0.8,0.733,0.6,0.52])

Temp = np.array([ 21, 22, 23, 26, 30, 37, 43, 48, 51, 57, 66, 75, 84, 90, 94, 98, 120, 
                 130, 140, 150, 160, 170, 180, 190, 200, 210, 220, 230, 240, 250])


R= np.array([ 274, 263, 254, 218, 181, 140, 105, 80.5, 74.8, 59, 42.1, 30.4, 22.5, 18.6, 
             16.2, 14.4, 6, 5, 4.7, 3.7, 3, 1.7, 1.5, 1.6, 1.1, 0.9, 0.08, 0.079,
             0.06, 0.05])

#Referenzwiderstand
Rn = 47 #/kOhm bei 21°C

x = log(R/Rn)
y = Temp
# Koeffizienten der Temperaturfunktion
poly = np.polyfit(x, y, 3)

# errechnete Temperatur
func = polyval(poly, x)

plt.figure(1)
plt.clf()
plt.plot(R, func,'r-')
plt.plot(R, Temp,'b-d')
plt.title('Kurvenschätzung')
plt.ylabel('Temperatur /Grad C')
plt.xlabel('Widerstand /Ohm')
plt.grid('on')

#Umess/1023 = R/(R+Rn)
#R/Rn = (Umess/1023) / (1-(Umess/1023))
#U = np.logspace(-1,0,20) # Umess/1023
U = np.linspace(0,1024)/1024. # Umess/1023 => /1024 um div/Null zu verhindern
RRn = U/(1-U)
plt.figure(2)
plt.clf()
plt.plot(U*1023,polyval(poly,log(RRn)),'b-d')
#fval = poly[2] + poly[1]*log(U) + poly[0]*log(U)**2
#plt.plot(U*1023,fval,'b-d')
plt.title('Messung -> Aufloesung in 1cnt')
plt.ylabel('Temperatur /Grad C')
plt.xlabel('Messwert')
plt.grid('on')

print 'temp = p3 + p2*ln(sVal) +p1*(ln(sVal))^2 +p0*(ln(sVal))^3'
print 'Parameter:'
print poly
