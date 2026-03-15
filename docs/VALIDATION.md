# Docksmith Validation Checklist

## Build cache checks
1. Run cold build: `docksmith build -t myapp:latest sample-app`
   - Expected: COPY/RUN steps show `[CACHE MISS]`.
2. Run warm build again.
   - Expected: COPY/RUN steps show `[CACHE HIT]`.
3. Edit `sample-app/message.txt` and rebuild.
   - Expected: COPY and all layer-producing steps below it show `[CACHE MISS]`.

## Runtime checks
1. Run default CMD: `docksmith run myapp:latest`
   - Expected: prints greeting and sample file content.
2. Run with env override: `docksmith run -e GREETING=Overridden myapp:latest`
   - Expected: greeting is overridden inside container.
3. Isolation check:
   - Update CMD temporarily (or run override command) to write a file under `/tmp`.
   - Verify host filesystem does not get that file.

## Images/rmi checks
1. `docksmith images`
   - Expected columns: Name, Tag, ID, Created.
2. `docksmith rmi myapp:latest`
   - Expected: manifest deleted and layer tar files removed.
