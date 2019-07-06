FROM bitcoin-black/btcb-env:gcc

ARG NETWORK=live
ADD ./ /tmp/src

RUN mkdir /tmp/build && \
    cd /tmp/build && \
    cmake /tmp/src -DBOOST_ROOT=${BOOST_ROOT} -DACTIVE_NETWORK=btcb_${NETWORK}_network && \
    make btcb_node && \
    make btcb_rpc && \
    cd .. && \
    echo ${NETWORK} > /etc/btcb-network

FROM ubuntu:16.04
COPY --from=0 /tmp/build/btcb_node /usr/bin
COPY --from=0 /tmp/build/btcb_rpc /usr/bin
COPY --from=0 /etc/btcb-network /etc
COPY docker/node/entry.sh /entry.sh
COPY docker/node/config /usr/share/btcb/config
RUN chmod +x /entry.sh
RUN ln -s /usr/bin/btcb_node /usr/bin/rai_node
CMD ["/bin/bash",  "/entry.sh"]
