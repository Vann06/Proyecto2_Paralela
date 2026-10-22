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

# Que MPI no intente comunicarse por loopback ni por la red interna de Docker
ENV OMPI_MCA_btl_tcp_if_exclude=lo,docker0
ENV OMPI_MCA_oob_tcp_if_exclude=lo,docker0
ENV OMPI_MCA_btl_vader_single_copy_mechanism=none

# 4. Código dentro de la imagen (sin volúmenes): mismo binario y misma ruta en todos los nodos
WORKDIR /app
COPY parte_1/ ./
RUN make \
    && mpicc -O2 -o bruteforce_newlib bruteforce_newlib.c -lcrypto

CMD ["/bin/bash"]
