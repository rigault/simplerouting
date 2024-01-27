import smtplib, sys
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart

smtp_server = "smtp.orange.fr"
smtp_port = 25
smtp_password = sys.argv [4]
smtp_username = "meteoinfoforrr@orange.fr"
from_email = "meteoinfoforrr@orange.fr"
message = MIMEMultipart()
message["From"] = from_email
message["To"] = sys.argv [1]
message["Subject"] = sys.argv [2]

message.attach(MIMEText(sys.argv [3], "plain"))

try:
   with smtplib.SMTP(smtp_server, smtp_port) as server:
      server.starttls()
      server.login(smtp_username, smtp_password.strip())
      server.send_message(message)
   sys.exit (0) # success

except Exception as e:
   print(f"Erreur lors de l'envoi du message : {e}")
   sys.exit (1)  # Échec

print("Message sent to: ", sys.argv [1], " subject: ", sys.argv [2], " body: ", sys.argv [3])
