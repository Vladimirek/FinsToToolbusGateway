# FINS To Toolbus Gateway

This TCP FINS server translate commands to serial Toolbus,read Tooolbus response and send back as FINS response.
It can be used with Sysmac OMRON PLC where no Ethernet is present.

Programmed in QT5.

This server is fully tested only with this FINS client implementation [1]. 
Note: As there is no official Toolbus documentation so undocumented behavior can occur. 

[1] https://bitbucket.org/vladimirek/omronlib
