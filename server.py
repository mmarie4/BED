#! /usr/bin/env python
# -*- coding: utf-8 -*-
from flask import *
from threading import Thread
import serial

# ------- Init ------------------------------------
# temp = {int nodeID: double temperature}
temp = {"1": "27.6", "2": "31.0", "3":"24.6"}

# =================================================
# Thread reading the temperature on the port
class Reader(Thread):
    def __init__(self, temp):
        Thread.__init__(self)
        self.temp = temp

    def run(self):
        while True:
            # read input instead of serial for testing
            '''
            id = input("type nodeID: ")
            temperature = input("type temperature: ")
            temp[id] = temperature
            print("temp[", id, "] :", temp[id])
            '''
            ser = serial.Serial('/dev/ttyACM0', 9600, timeout=1)

            # Handshake
             ok = b''
            while ok.strip() != b'OK':
                ser.write(b"1")
                ok = ser.readline()
                print("Handshake OK!\n")

            print("connected to: " + ser.portstr)

            while True:
                line = ser.readline()
                # parse nodeID et temperature
                # temp[nodeID] = temperature 

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
            str_keys = ""
            str_values = ""
            for key in temp.keys():
                str_keys += key + " "
            for val in temp.values():
                str_values += val + " "
            return str_keys[:-1] + "-" + str_values[:-1]

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
