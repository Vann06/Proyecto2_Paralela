FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# 1. Dependencias base (+ libssl-dev para OpenSSL, iproute2/ping para diagnosticar la red)
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        openmpi-bin \
        libopenmpi-dev \
        openssh-server \
        openssh-client \
        libssl-dev \
        iproute2 \
        iputils-ping \
        nano \
    && rm -rf /var/lib/apt/lists/*

# 2. SSH sin intervención
#    - PermitRootLogin yes en el servidor
#    - StrictHostKeyChecking no en el cliente
#    - Puerto 2222: con --network host el contenedor comparte los puertos del host,
#      y si el host ya tiene su propio sshd en el 22 el del contenedor no arranca
RUN mkdir -p /var/run/sshd /root/.ssh && chmod 700 /root/.ssh \
    && sed -i 's/^#\?PermitRootLogin.*/PermitRootLogin yes/' /etc/ssh/sshd_config \
    && sed -i 's/^#\?Port .*/Port 2222/' /etc/ssh/sshd_config \
    && printf 'Host *\n    Port 2222\n    StrictHostKeyChecking no\n    UserKnownHostsFile /dev/null\n    LogLevel ERROR\n' >> /etc/ssh/ssh_config

# 3. Ejecución como root en OpenMPI
ENV OMPI_ALLOW_RUN_AS_ROOT=1
ENV OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1

# hwloc intenta abrir las pantallas X11 (:0, :1, ...) al arrancar; en WSL esas
# conexiones se quedan colgadas y orted nunca termina de iniciar
ENV HWLOC_COMPONENTS=-gl
ENV OMPI_MCA_btl_vader_single_copy_mechanism=none

# mpirun lanza orted en los otros nodos por SSH, y esas sesiones no heredan los ENV
# de Docker: se repiten en /etc/environment (lo lee PAM al iniciar sesión SSH) y en
# el archivo de parámetros de OpenMPI
RUN echo "HWLOC_COMPONENTS=-gl" >> /etc/environment \
    && printf '\nbtl_vader_single_copy_mechanism = none\n' >> /etc/openmpi/openmpi-mca-params.conf

# 4. Código dentro de la imagen (sin volúmenes): mismo binario y misma ruta en todos los nodos
WORKDIR /app
COPY parte_1/ ./
COPY parte_2/hostfile ./
RUN make \
    && mpicc -O2 -o bruteforce_newlib bruteforce_newlib.c -lcrypto

CMD ["/bin/bash"]
