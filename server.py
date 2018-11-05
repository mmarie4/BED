#! /usr/bin/env python
# -*- coding: utf-8 -*-
from flask import *
from threading import Thread
import serial

# ------- Init ------------------------------------
# temp = {int nodeID: double temperature}
temp = {}   

# =================================================
# Thread reading the temperature on the port
class Reader(Thread):
    def __init__(self, temp):
        Thread.__init__(self)
        self.temp = temp

    def run(self):
        while True:
            ser = serial.Serial('/dev/ttyACM0', 9600)

            while True:
                line = ser.readline().decode('utf-8')
                # parse nodeID et temperature
                # # Messages : type:rank:nodeID:value
                data = line.split(":")
                print("data read on serial port:", line)
                if(data[0] == "2"):
                    nodeID = data[2]
                    temperature = data[3]
                    temperature = temperature.replace("\r\n", "")
                    temperature = float(temperature)/10
                    print(nodeID, temperature)
                    self.temp[nodeID] = str(temperature)
            ser.close()

# =================================================



# =================================================
# Thread running the server
class Server(Thread):
    def __init__(self, temp):
        Thread.__init__(self)
        self.temp = temp

    def run(self):
        # ------- Create Flask server ---------------------
        app = Flask(__name__)
        app.secret_key = 'stytjyntil468kyjnmti65468'

        # ------- Routes ----------------------------------
        @app.route('/<nodeID>')
        def node(nodeID):
            if(nodeID in self.temp):
                return self.temp[nodeID]
            else:
                return "wrong id"
        
        @app.route('/all')
        def index():
            str_temp = ""
            for key in temp.keys():
                str_temp += key + ":" + temp[key] + "\r\n"
            return str_temp

        # -------------- Launch app ------------------------
        if __name__ == '__main__':
            app.run(debug=False)

# =================================================

# ------- Launch threads -------------------
r_thread = Reader(temp)
s_thread = Server(temp)
r_thread.start()
s_thread.start()
r_thread.join()
s_thread.join()
