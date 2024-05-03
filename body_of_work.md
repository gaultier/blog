# Body of work

I am here recounting chronologically what I have achieved in my career until now. I have a sub par memory so it's great for me to look back on that, and may it also hopefully serve as an interesting insight for fellow practitioners and recruiters into the kind of work I enjoy and am experienced with.

##  Crédit Mutuel (Bank), Strasbourg, France; 2013

This was my first professional experience (a 8 weeks internship) and happened at one of the big banks in the country. Like most banks that have been founded in the 20th century, they have millions and millions of lines of code in COBOL, and realized this is not tenable and need to migrate to a modern tech stack to be able to keep it simply running. 

I migrated a business application used internally. The original application was a terminal (as in: made for a *hardware* terminal), with a basic UI, talking to a DB2 database, and running on IBM's z/OS on a (real) mainframe in the confines of the bank. The final application was a C# web application, still talking to the same database, but this time running on a Windows server.

In retrospect, that was such a unique experience to work on a 30+ year old codebase running on a tech stack, OS and hardware that most developers will never encounter.

The team insisted (although not unanimously) that I write *new* COBOL code for this brand new applications for the layer that talks to the database. I think they were worried that C# could not do this job properly, for some reason? So I got to do that, in a COBOL IDE made by IBM which only COBOL developers know of. To-do list item ticked, I guess. 

All in all, that was a very interesting social experience and a great insight on how business and developers think and (try to) evolve, and how one can attempt to change the tech stack of an existing running application, which is a challenge any company will face at a moment or another. And how we engineers have a professional duty to keep learning and adapting to this changing world.

---

## CNRS Intern Software Engineer experimenting with the Oculus Rift (VR) CNRS, Strasbourg, France; 2014

My second internship (10 weeks), and perhaps the project I loved the most. This took place at an astronomy lab, I had the incredible privilege to have my office in the old library that was probably a few centuries old, filled with old books; the building was this 19th century observatory with a big park with bee houses... This will never be topped.

The work was also such a blast: I got to experiment with the first version of the Oculus Rift, and tinker with it. My adviser and I decided to work on two different projects: A (from scratch) 3D visualization of planets inside the Oculus Rift for kids to 'fly' through the solar system and hopefully spark in them an interest in space exploration and astronomy. The second project was to add to an existing and large 3D simulation of planets a VR mode.

It was an exciting time, VR was all the rage and everything had to be figured out: the motion sickness, the controller (it turns out that most people are not so good at using a keyboard and mouse while being completely blind and a video game console controller is much more intuitive), how to plug the Oculus Rift SDK to an existing codebase, the performance, etc.

Even though I wished I had a tad more time, I delivered both. Since we foresaw we would not have time for everything, the solar system visualization got cut down to blue sparkling cubes in 3D space, but still, the Oculus Rift worked beautifully with it. Even though the field was in its infancy, the feeling of realism was already jarring - our brain is tricked easily! The second project also worked, although we had visual artifacts in some cases when traveling far distances, which I suspected was due to floating point precision issues in the existing codebase. There are articles online discussing this fact with physical simulations where huge distances are present and I discussed it with codebase authors during our weekly check-ins and demo sessions.

Performance was initially also a challenge since VR consists of rendering the same scene twice, once for each eye, with a slight change in where the camera is in 3D space (since the camera is your eye, in a way). And 3D rendering will always be a domain where performance is paramount. It's interesting to note that new 3D APIs such as Vulkan do offer features for VR in the form of extensions to speed it up in hardware, having the GPU do the heavy lifting. But back in 2014, there was nothing like that. Also, 3D APIs have really evolved in the last decade, becoming more low level and giving the developer more control, power, but also responsibilities.

My major performance stepping stone was moving from rendering everything in the scene to using an octree to only render entities in the 'zone' where the camera is, or is looking at.

I used OpenGL and C++ for the first project, and C for the second one since the existing codebase was in C.

3D, VR, extending an existing codebase, starting a new project from scratch with 'carte blanche': I learned a ton! And my adviser, fellow coworkers exploring this space (notably trying to do the same with a different VR headset, the Sony Morpheus), and I even got to publish a paper based on our work, that got submitted: [Immersive-3D visualization of astronomical data](cnrs.pdf), [link 1](https://ui.adsabs.harvard.edu/abs/2015ASPC..495..125S/abstract), [link 2](http://aspbooks.org/custom/publications/paper/495-0125.html).


Finally, I on-boarded my successor on the codebase and the build system and helped them troubleshoot some cross-platform issues.

---

## Full-stack Software Engineer EdgeLab, Lausanne, Switzerland; 2015-2017

My first full-time job, initially being a 6 months internship concluding my Master's degree of Computer Science, and then extending into a full time position. The company was a financial startup building simulations of the stock market: how does the price of a bond or an option evolve if there is an earthquake or a housing crash? The idea was to observe how these events affected the stock market historically and simulate these happening on your portfolio to get a sense of how robust or risky your positions are.

The startup was filled with super smart Math and Physics doctors and it was such a chance to work alongside them.

It was lots of new stuff for me: new country, new way of working, being in a small startup (something like 10 people when I joined, if at all), and having to ship something very quickly for a demo coming up in a few days!

My main achievements were at first to optimize and simplify the front-end experience which had complex and at times slow pages (I remember the infamous Tree Table: a classic HTML table, except that each cell shows tree shaped data, like a file system!). Then, realizing it would not scale easily, I convinced the team to migrate to Typescript (that's quite early at this point: end of 2015!). That was so effective that while the migration was on-going, you could tell which page of the application was written in JavaScript or Typescript based on whether it had random bugs or not. Most of the gains of Typescript was not so much the readability of static typing or the warnings, although these helped, but rather reducing the dynamism of the code by forcing the developer to stick to one type for a given variable. That of course dramatically improved correctness and reduced bugs, but incidentally also helped the performance!

I then moved to the back-end, extending the financial models and simulations in C++. That was a lot of matrix code and financial math to learn!
I also contributed to modernizing the codebase to C++11 and improving the build system to make it easier to on-board new people.


Eventually, the company got acquired for 8 digits by a Swiss bank.

---

## Back-end Software Engineer & DevOps PPRO, Munich, Germany; 2017-2023

I knew I did not want to stay in Switzerland, and found a job as a Software Engineer in Munich, Germany, at a FinTech company. This time not the stock market kind but the online payments kind. Think Paypal or the now defunct Wirecard (but we were honest and law abiding, and they were not).

I joined to kick start the effort of transforming web applications from server-side rendered HTML with a tiny bit of JavaScript to fully dynamic single page applications (SPA) in Typescript. At the time, SPAs were all the rage, and it is true that C++ back-ends with a slow and complex deployment process are not a great fit to a fast growing company. Still, looking back, I am not sure if static HTML does not cover 90% of the use cases. However, it was so much fun to get feedback or a request from the users, implement it in a few hours, deploy it, and tell them to try it! They got used to that rapid cycle very quickly.

I then moved to the back-end, working for a time in a full-stack manner, and then spear-heading the company-wide effort to move to the cloud and Kubernetes, thus I morphed into a DevOps person, migrating without any downtimes numerous applications. Without the customers even noticing! 
I also trained and helped other teams to adopt Kubernetes and the cloud, conducted very many interviews that resulted in hiring a number of very fine folks that I still hold dear to my heart to this day.

I then again spear-headed a new transformation: Adopting the JVM, seen as more fitting to 'micro'-services than C++, more specifically Kotlin.

One Kotlin application I worked on was an internal (soft) real-time ledger application for accounting and compliance purposes using the Cassandra database and then Postgres. 

After deploying several production Kotlin services, I moved to the payment platform team where I led a brand new transformation (again!): Moving from batch services running at night (and usually being troubleshooted during the day), to a real time event based architecture using Kafka (and then later Kinesis).

My work focused on writing from scratch the main producer of these events in Go: a bridge from a traditional RDBMS, to Kafka; as well as educating consumers on this new way of writing software. Challenges were plentiful: Events had to be first manually added to the existing C++ payment software, tracking down each location that mutated data and storing the right event in the database, without breaking the crucial payment flows. Then, our bridge would poll multiple such databases in multiple datacenters, (the databases being of course different RDBMS, versions, and OSes!), exporting (i.e. producing) in a live fashion these events to Kafka, 24/7/365. 

As more and more services consuming these events blossomed in the company in various teams (be it reporting, billing, compliance, different internal UIs, etc), correctness, reliability and performance were paramount and I made sure that we nailed these factors, among other ways, by improving observability (with metrics, logs, alerts, and opentelemetry), and by constant profiling. The surest way to care about the quality of your software is to be on-call for it, and I was.

Another achievement of mine is helping a coworker finish a compliance project analyzing in real time each payment events being produced by my application and, based on rules conceived with business experts, deciding if a payment is fraudulent or not, potentially blocking it from progressing further and notifying a human to inspect it.

Finally, I helped move the observability stack the company used to Datadog (by comparing several alternatives) and experimented with the serverless architecture by writing and deploying a production lambda in Go which ran flawlessly for months and never had an issue, costing less than 10$ to the company monthly, when the company was contemplating this path.

I also had a short stint as a team manager, but after 2 months I decided this was not for me and stepped down. I am a Software Engineer and practitioner at heart.

I look fondly on all these achievements, achieving business targets with a variety of tech stacks and cloud services, majorly contributing to fundamentally transform the company from a slow moving, datacenter based software stack, where developers sometimes wait for weeks for one deployment to happen, and a new project is a herculean effort of synchronizing every team; to a fast-moving, cloud based, self-service and event-oriented architecture, where each team is autonomous, gets a real-time stream of events containing all the information they need, and has nigh complete control and visibility on the whole lifecycle of their application.

---

## Senior Software Engineer Giesecke+Devrient, Munich, Germany; 2023-present

After 6+ years in my job, I decided I was ready for the next challenge and joined my current company to help productionize an innovative Central Bank Digital Currency project (similar to a cryptocurrency, but backed by a sovereign state, using the state's currency, and with all the high standards one expects from regulated financial institutions). The most interesting feature, technically and product wise, is offline payments (card to card) with a focus on privacy. 

My first focus has been making the product more reliable and fast, relentlessly optimizing the performance and memory usage to ensure that an entire nation can use this suite of applications 24/7, surviving network disruptions, datacenter disasters, etc; while adding crucial business features such as non-repudiation of payments.

My second focus has been security: going through regular threat analysis exercises with the team, adding scanning of dependencies and docker images to every project in order to find vulnerabilities or insecure code patterns, fuzzing, etc.

Indeed, I inherited a C++ codebase where the original author moved on, which was a central part of the company's offering. After investing some time to get it up to modern standards, I convinced stakeholders and developers to incrementally rewrite it in a memory-safe language (Rust). I led this effort by mentoring fellow developers, establishing a roadmap, doing the implementation work, and presenting regular demos of the progress to stakeholders. This software runs on various OSes and architectures including mobile platforms.
