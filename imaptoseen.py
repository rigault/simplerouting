import imaplib, sys

email_user = 'rene.rigault@wanadoo.fr'
email_password = sys.argv [1]
mail_server = 'imap.orange.fr'
mailbox = 'inbox/meteoinfoforrr@orange.fr'

# Connexion au serveur IMAP
mail = imaplib.IMAP4_SSL(mail_server)
mail.login (email_user, email_password)

# Sélection de la boîte de réception
mail.select (mailbox)

# Recherche de tous les messages non lus (UNSEEN)
status, messages = mail.search (None, 'UNSEEN')

if status == 'OK':
    # Marquer tous les messages comme lus (SEEN)
    for num in messages[0].split():
        mail.store(num, '+FLAGS', r'(\Seen)')
    
    # print("In imaptoseen.py: Tous les messages sont avec marque lus.")

# Fermer la connexion
mail.logout()
