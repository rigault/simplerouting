import imaplib, email, sys, os
from email.header import decode_header

# Paramètres de connexion
email_user = 'rene.rigault@wanadoo.fr'
email_password = sys.argv [2]
mail_server = 'imap.orange.fr'
mail_box = 'inbox/meteoinfoforrr@orange.fr'

# Fonction pour décoder le sujet du mail
def decode_subject(encoded_subject):
   if isinstance(encoded_subject, bytes):
      decoded_tuple = decode_header(encoded_subject)[0]
      decoded_str, encoding = decoded_tuple
      if encoding is not None:
         return decoded_str.decode(encoding)
      else:
         return decoded_str
   return encoded_subject

# Connexion au serveur IMAP et selection boite
mail = imaplib.IMAP4_SSL(mail_server)
mail.login (email_user, email_password.strip ())
mail.select (mail_box)

#print ("pw", sys.argv [2], "End")
# Recherche des emails venant de toto@orange.fr et contenant "'gfs" dans l'objet
#status, messages = mail.search(None, '(FROM "meteoinfoforrr@orange.fr" SUBJECT "")')
#status, messages = mail.search(None, '(SUBJECT "gfs")')
status, messages = mail.search (None, 'UNSEEN')

# Liste des ID des messages correspondants
message_ids = messages[0].split()

# Parcours des messages
for msg_id in message_ids:
   # Récupération du message
   status, msg_data = mail.fetch(msg_id, '(RFC822)')
   raw_email = msg_data[0][1]
   
   # Parsage du message
   msg = email.message_from_bytes(raw_email)
   
   # Récupération de l'émetteur et du sujet
   sender = msg.get("From")
   subject = decode_subject(msg.get("Subject"))
   
   # Affichage à la console
   print ("Provider:", sender)
   print ("Subject:", subject)
   
   # Sauvegarde des pièces jointes
   for part in msg.walk():
      if part.get_content_maintype() == 'multipart':
         continue
      if part.get('Content-Disposition') is None:
         continue
      filename = part.get_filename()
      if filename:
         #filepath = os.path.join('/home/rr/routing/grib', filename)
         filepath = os.path.join (sys.argv [1], filename)
         print ("File:", os.path.abspath(filepath), " ") # space is required
         with open(filepath, 'wb') as f:
            f.write(part.get_payload(decode=True))
   mail.store(msg_id, '+FLAGS', '(\Deleted)')

mail.expunge()
mail.logout()

