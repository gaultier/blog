/* pid$target::search_index_feed_document:entry { self->t=1 } */
/* pid$target::search_index_feed_document:return { self->t=0 } */

BEGIN {
  size = 0ULL;
}

pid$target::pg_realloc:entry {
  this->size = arg2*arg4;
  printf("realloc: sizeof=%d count=%d\n",arg2,arg4);
  ustack();
  size += this->size;

  @reallocs[ustack()] = quantize(this->size);
}

pid$target::pg_alloc:entry {
  this->size = arg1*arg3;
  /* @alloc_sizes = quantize(this->size); */
  /* @alloc_count=quantize(arg3); */
  /* @alloc_sizeof=quantize(arg1); */

  /* if (this->size > 8192) { */
  /*   printf("alloc: sizeof=%d count=%d\n",arg1,arg3); */
  /*   ustack(); */
  /* } */
  size += this->size
}

END {
  printf("total=%llu\n", size);
}
