# BED
Embedded programm for Raspberry Pi and msp ez430

# Python server #
Web service qui renvoie la temperature de chaque noeud

Deux threads :
  - r_thread qui lit sur le port série les data des capteurs et met à jour le dictionnaire temp (key=nodeID, value=temperature)
  
  - s_thread qui fait tourner le serveur Flask pour le web service 
    - Route /X renvoie la température associée au node qui a pour id l'entier X (ex.: localhost:5000/2 renvoie temp[2])
    - Route /all renvoie une chaine de caractères de la forme : node1:temp1\r\nnode2:temp2\r\n (qui est donc facilement parsable en utilisant ":" et "\r\n" comme séparateurs)
  
  
# Todo
Dossier à placer dans ez430-applications-students
