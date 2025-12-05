# 🛡️ Documentation Serveur & Infrastructure

**Date de mise à jour :** 26 novembre 2025  
**Serveur :** vps-1 (Oracle Cloud)  
**IP Publique :** 141.145.194.69  
**OS :** Rocky Linux 9.6 (Blue Onyx)  
**Score de Sécurité (Lynis) :** 83/100

-----

## 🎯 Philosophie et Principes d'Administration

### Objectifs Principaux

Ce serveur est configuré pour la **production**, avec un équilibre strict entre **sécurité maximale** et **accessibilité garantie**.

**1. Cohérence et Intelligence**

  - Chaque règle a une raison d'être documentée.
  - Pas de "copier-coller" aveugle : on comprend ce qu'on applique.
  - Privilégier les solutions robustes (Cloudflare Tunnel, SSH Key) aux bricolages.

**2. Ne JAMAIS se bloquer l'accès**

  - **Règle d'or :** Toujours garder une session SSH ouverte lors des modifications réseau/SSH.
  - L'utilisateur `rocky` est explicitement autorisé dans `sshd_config`.

**3. Sécurité Progressive**

  - Défense en profondeur (Defense in Depth) : Si une barrière tombe (ex: Pare-feu), une autre est là (Fail2ban, Auth 2FA).
  - Surfaces d'attaque réduites au minimum (Ports fermés, Tunneling).

-----

## 🔑 Accès & Connexion

### Accès SSH (Administration)

L'authentification par mot de passe est **désactivée**. Seule la clé cryptographique fonctionne.

```powershell
# Depuis Windows PowerShell
ssh rocky@141.145.194.69
```

**Commandes rapides (One-shot) :**
Pour la maintenance sans ouvrir de shell interactif :

```powershell
ssh rocky@141.145.194.69 "sudo systemctl status meshcentral"
```

-----

## ☁️ Infrastructure Cloudflare Tunnel (Nouveau)

**Rôle :** Sécurise l'accès web sans ouvrir de ports publics critiques.  
**Service :** `cloudflared`  
**État :** Actif (Tunnel UUID lié au compte Cloudflare Zero Trust)

**Fonctionnement :**

1.  Le trafic arrive sur `mesh.lgtw.tf` (Géré par Cloudflare).
2.  Cloudflare filtre les menaces (DDoS, Bots).
3.  Le trafic passe dans un tunnel chiffré vers le VPS.
4.  Le VPS reçoit la requête sur `localhost:443`.

**Commandes utiles :**

```bash
sudo systemctl status cloudflared  # Vérifier que le tunnel est vert
sudo journalctl -u cloudflared -f  # Voir le trafic passer en temps réel
```

-----

## 🖥️ MeshCentral - Gestion à Distance

### Configuration

**Version :** MeshCentral 1.1.53  
**URL :** [https://mesh.lgtw.tf](https://mesh.lgtw.tf)  
**Infrastructure :** Derrière Cloudflare Tunnel (Pas de port 443 ouvert en public sur le VPS).  
**Dossier :** `/opt/meshcentral/`

### Sécurité Active

  - **Proxy :** Configuré pour faire confiance aux IPs Cloudflare (`"trustedProxy": "CloudFlareIPs"`).
  - **SSL :** Géré par Cloudflare (Edge) + Auto-signé en local (accepté par le tunnel via `NoTLSVerify`).
  - **Auth :** 2FA Obligatoire (TOTP).
  - **Session :** IP Check Strict, Cookies Secure.

### Fichier de Config (`config.json`)

Emplacement : `/opt/meshcentral/meshcentral-data/config.json`

**Paramètres clés :**

```json
"settings": {
    "cert": "mesh.lgtw.tf",
    "WANonly": true,
    "port": 443,
    "trustedProxy": "CloudFlareIPs",  <-- Vital pour le Tunnel
    "certUrl": "https://mesh.lgtw.tf",
    "ignoreAgentHashCheck": true,
    "sessionSameSite": "strict",
    "cookieIpCheck": "strict"
}
```

### Thème Graphique (Stylish UI)

Le thème "Modern UI" est installé via le repo `Melo-Professional`.

  - **Mise à jour du thème :**
    ```bash
    cd /opt/meshcentral/MeshCentral-Stylish-UI && git pull
    cp -r meshcentral-web/public/* ../meshcentral-data/meshcentral-web/public/
    sudo systemctl restart meshcentral
    ```

-----

## 🛡️ Sécurisation Système (Hardening)

### 1\. SSH Durci

Fichier : `/etc/ssh/sshd_config.d/99-hardening.conf`

  - **Root Login :** `No`
  - **Password Auth :** `No` (Clés uniquement)
  - **AllowUsers :** `rocky` (Liste blanche stricte)
  - **MaxAuthTries :** 3

### 2\. Pare-feu (Firewalld)

Stratégie de **Whitelist** : Tout est fermé sauf ce qui est explicitement ouvert.

**Ports Ouverts :**

  - **22/tcp** (SSH) : Administration
  - *Note : Les ports 80/443 sont fermés ou filtrés car MeshCentral passe par le Tunnel.*

<!-- end list -->

```bash
sudo firewall-cmd --list-all
```

### 3\. Fail2Ban (Intrusion Prevention)

Bannissement automatique des IPs tentant de forcer l'accès.

  - **SSH Jail :** 3 tentatives ratées = Ban 2h.
  - **Backend :** Systemd + Firewalld.

<!-- end list -->

```bash
sudo fail2ban-client status sshd
sudo fail2ban-client unban <IP>  # En cas d'erreur
```

### 4\. Kernel & Sysctl

Protection contre les attaques réseaux (Spoofing, MITM, SYN Flood).
Fichier : `/etc/sysctl.d/99-hardening.conf`

  - Désactivation du routage IP.
  - Protection contre les redirections ICMP.
  - Masquage des pointeurs kernel (`kptr_restrict`).

-----

## 🔧 Maintenance & Dépannage

### Mises à jour (Hebdomadaire)

```bash
# Mettre à jour le système
sudo dnf update -y

# Vérifier qu'un reboot n'est pas requis (si nouveau kernel)
sudo needs-restarting -r
```

### Problème : "Je n'arrive plus à me connecter à MeshCentral"

1.  Vérifier si le Tunnel tourne :
    `sudo systemctl status cloudflared`
2.  Vérifier si MeshCentral tourne :
    `sudo systemctl status meshcentral`
3.  Vérifier les logs d'erreurs :
    `sudo journalctl -u meshcentral -e`

### Problème : "Connexion instable sur le réseau du lycée (Wi-Fi)"

**Symptôme :** Ça marche, puis ça bloque, puis ça remarche après avoir attendu.

**Explication :** C'est le jeu du chat et de la souris avec le pare-feu du lycée.
1.  **Loterie des IPs Cloudflare :** MeshCentral utilise Cloudflare. Le lycée bloque certaines IPs de Cloudflare, mais pas toutes. Quand tu réessaies, tu tombes parfois sur une "bonne" IP.
2.  **Analyse de trafic :** Le pare-feu peut couper la connexion s'il détecte une session trop longue (WebSocket).

**Solution :**
*   Patience : Déconnecter/Reconnecter le Wi-Fi pour changer d'IP de sortie ou forcer une nouvelle résolution DNS.
*   VPN : Utiliser un VPN sur le PC client (si non bloqué) contourne ce filtrage.

-----

## 🏗️ Build & Déploiement de l'Agent

### 1. Compilation Locale (Windows)

Pour compiler l'agent avec les dernières modifications (correctifs KVM, fenêtres cachées, etc.) :

1.  Ouvrir le projet dans Visual Studio ou utiliser MSBuild.
2.  Solution : `MeshAgent-2022.sln`
3.  Configuration : `Release` / `x64` (ou `x86` selon la cible).

**Commande MSBuild :**
```powershell
msbuild MeshAgent-2022.sln /p:Configuration=Release /p:Platform=x64
```

L'exécutable généré se trouvera dans : `MeshAgent/x64/Release/MeshAgent.exe` (ou chemin similaire selon la config).

### 2. Test Local

Pour tester sans déployer sur le serveur :
1.  Arrêter le service MeshAgent local s'il tourne (`sc stop "Mesh Agent"`).
2.  Remplacer l'exécutable local par le nouveau build.
3.  Lancer l'agent en mode console pour voir les logs : `MeshAgent.exe -run`
4.  Vérifier que les crashs UAC sont résolus et que les fenêtres terminal sont cachées.

### 3. Déploiement sur le Serveur

Une fois validé, pour mettre à jour l'agent distribué par le serveur :

1.  Se connecter au serveur : `ssh rocky@141.145.194.69`
2.  Aller dans le dossier des agents : `/opt/meshcentral/meshcentral-data/agents/` (chemin à vérifier selon l'installation).
3.  Remplacer le fichier `MeshAgent-Windows64.exe` (ou équivalent) par le nouveau build.
4.  MeshCentral proposera automatiquement la mise à jour aux agents connectés (sauf si `ignoreAgentHashCheck` empêche la détection stricte, mais le changement de binaire devrait être notifié).
