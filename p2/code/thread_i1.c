/*
    queue* arrival, sheduled;
    ucontext *scheduler;
    ucontext *cleanup;

    tcb * running;

    function initialize_library():
      arrival, scheduled = malloc(sizeof(queue))
      init_queue(arrival)
      init_queue(scheduled)

      tcb* running = malloc(sizeof(tcb));
      running.ucontext = getcontext(); // main 
      makecontext with schedule function
      enqueue(arrival, running) // main
      
      initialize scheduler context off of main 

        
    function worker_create_tcb (params) -> tcb * :
      tcb* = malloc()
      ucontext etc
      tcb->ucontext = etc...
      returns tcb pointer

    function worker_create:
      tcb* new_worker = worker_create_tcb
      new_worker->uc_link = cleanup context;
      if (scheduler == NULL) {
        // implies running is null as well
        initialize_library()
      } 

      enqueue(arrival, new_worker)
      swapcontext(running->ucontext, scheduler);

    function schedule() {
      while(1):
        if arrival union scheduled is null: // main also done
           exit program swapfile

        // include appropriate state changes here
        enqueue(scheduled, running);
        running = dequeue(scheduled);
        swapcontext(scheduler, running->ucontext)
    }

    function performcleanup():
      while(1):
        uint8 tid_ended = tcb->tid
        free heap of running tcb, free tcb
        join search: if any worker waiting for this to complete (via join) - then set that state to ready
        swap  context to scheduler // note that scheduler cleans up after itself
*/
