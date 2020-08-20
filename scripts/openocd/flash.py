# flash.py <hex_filename>

import os, sys, time, telnetlib

cmd = 'reset init ; flash write_image erase %s ; reset\n' % sys.argv[1]

# Start the OpenOCD daemon in the background and connect via telnet
def open_ocd():
    os.system('openocd -f scripts/openocd/f1.cfg &')
    while True:
        time.sleep(0.5)
        try:
            t = telnetlib.Telnet('localhost', 4444)
        except:
            pass
        else:
            return t

with open_ocd() as t:
    t.write(cmd.encode('utf-8'))
    t.write('shutdown\n'.encode('utf-8'))
    t.read_all() # Waits for EOF (telnet session shutdown)
