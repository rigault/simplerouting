import smtplib, sys
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart

# Informations sur le serveur SMTP
smtp_server = 'smtp.orange.fr'
smtp_port = 465  # Port SSL pour Orange

# Informations d'authentification
smtp_username = "meteoinfoforrr@orange.fr"
smtp_password = sys.argv [4]

# Destinataire et expéditeur
from_email = "meteoinfoforrr@orange.fr"
to_email = sys.argv [1]

# Création du message
msg = MIMEMultipart()
msg['From'] = from_email
msg['To'] = sys.argv [1]
msg['Subject'] = sys.argv [2]

# Corps du message
body = sys.argv [3]
msg.attach(MIMEText(body, 'plain'))

# Connexion au serveur SMTP sécurisé
server = smtplib.SMTP_SSL(smtp_server, smtp_port)

# Authentification
server.login(smtp_username, smtp_password)

# Envoi du message
server.sendmail(from_email, to_email, msg.as_string())

# Fermeture de la connexion
server.quit()
print ("In smtp.py: Message sent to on port ", smtp_port, ": ", sys.argv [1], " subject: ", sys.argv [2], " body: ", sys.argv [3])
