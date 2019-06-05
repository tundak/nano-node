#!/bin/bash
set -e

scripts="$(dirname "$0")"

if [ -n "$DOCKER_PASSWORD" ]; then
    echo "$DOCKER_PASSWORD" | docker login -u btcbreleaseteam --password-stdin

    # We push this just so it can be a cache next time
    if [ "$TRAVIS_BRANCH" = "master" ]; then
        for compiler in gcc clang; do
            ci_image_name="btcbcurrency/btcb-ci-${compiler}"
            ci/build-docker-image.sh docker/ci/Dockerfile-${compiler} btcbcurrency/btcb-ci-${compiler};
            "$scripts"/custom-timeout.sh 30 docker push "$ci_image_name"
        done
    fi

    tags=()
    if [[ "${TRAVIS_TAG}" =~ 'RC' ]]; then
        tags+=("$TRAVIS_TAG" latest-including-rc)
    elif [ -n "$TRAVIS_TAG" ]; then
        tags+=("$TRAVIS_TAG" latest latest-including-rc)
    elif [ -n "$TRAVIS_BRANCH" ]; then
        tags+=("$TRAVIS_BRANCH")
    fi

    for network in live beta; do
        if [ "${network}" = 'live' ]; then
            network_tag_suffix=''
        else
            network_tag_suffix="-${network}"
        fi

        docker_image_name="btcbcurrency/btcb${network_tag_suffix}"

        "$scripts"/custom-timeout.sh 30 docker build --build-arg NETWORK="$network" -f docker/node/Dockerfile -t "$docker_image_name" .
        for tag in "${tags[@]}"; do
            # Sanitize docker tag
            # https://docs.docker.com/engine/reference/commandline/tag/
            tag="$(printf '%s' "$tag" | tr -c '[a-z][A-Z][0-9]_.-' -)"
            if [ "$tag" != "latest" ]; then
                docker tag "$docker_image_name" "${docker_image_name}:$tag"
            fi
            "$scripts"/custom-timeout.sh 30 docker push "${docker_image_name}:$tag"
        done
    done

fi