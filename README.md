# BED
Embedded programm for Raspberry Pi and msp ez430

# Python server #
Web service qui renvoie la temperature de chaque noeud

Deux threads :
  - r_thread qui lit sur le port série les data des capteurs et met à jour le dictionnaire temp (key=nodeID, value=temperature)
  
  - s_thread qui fait tourner le serveur Flask pour le web service 
    - Route /<nodeID> renvoie la température associée au node qui a pour id nodeID
    - Route /all renvoie une chaine de caractères de la forme : node1 node2 node3-temp1 temp2 temp3 (qui est donc facilement parsable en utilisant " " et "-" comme séparateurs)
  
  
# Todo
- Penser à installer serial sur le Raspberry Pi --> pip install pyserial
- Vérifier que ça lit bien sur le port serial les infos envoyées par le msp
- Changer le format des messages envoyés par le msp pour qu'ils soient facilement parsable par le Raspberry Pi 
- Parser les messages dans le server.py et update le dictionnaire temp
